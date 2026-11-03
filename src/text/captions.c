/* Port of src/text/captions.ts. GPL-2.0-or-later. */
#include "qa/text/captions.h"

#include <math.h>
#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static noreturn void qa_error(const char *format, ...) {
  char text[1536];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof text, format, args);
  va_end(args);
  qa_raise("Error", 0, 0, "%s", text);
}

static char *dup_range(const char *text, size_t length) {
  char *out = (char *)malloc(length + 1);
  if (out != NULL) {
    memcpy(out, text, length);
    out[length] = '\0';
  }
  return out;
}
static char *dup_text(const char *text) {
  return text != NULL ? dup_range(text, strlen(text)) : NULL;
}

void qa_caption_cue_clear(qa_caption_cue *cue) {
  free(cue->id);
  free(cue->text);
  free(cue->speaker);
  for (size_t i = 0; i < cue->argument_count; i++) free(cue->arguments[i]);
  free(cue->arguments);
  memset(cue, 0, sizeof *cue);
}

void qa_caption_cues_free(qa_caption_cue *cues, size_t count) {
  for (size_t i = 0; i < count; i++) qa_caption_cue_clear(&cues[i]);
  free(cues);
}

static bool cue_copy(const qa_caption_cue *source, qa_caption_cue *out) {
  memset(out, 0, sizeof *out);
  out->kind_caption = source->kind_caption;
  out->start_milliseconds = source->start_milliseconds;
  out->duration_milliseconds = source->duration_milliseconds;
  out->id = dup_text(source->id != NULL ? source->id : "");
  out->text = dup_text(source->text != NULL ? source->text : "");
  out->speaker = source->speaker != NULL ? dup_text(source->speaker) : NULL;
  if (out->id == NULL || out->text == NULL ||
      (source->speaker != NULL && out->speaker == NULL)) {
    qa_caption_cue_clear(out);
    return false;
  }
  if (source->argument_count != 0) {
    out->arguments =
        (char **)calloc(source->argument_count, sizeof *out->arguments);
    if (out->arguments == NULL) {
      qa_caption_cue_clear(out);
      return false;
    }
    out->argument_count = source->argument_count;
    for (size_t i = 0; i < source->argument_count; i++) {
      out->arguments[i] =
          dup_text(source->arguments[i] != NULL ? source->arguments[i] : "");
      if (out->arguments[i] == NULL) {
        qa_caption_cue_clear(out);
        return false;
      }
    }
  }
  return true;
}

static void validate_cue(const qa_caption_cue *cue) {
  size_t id_length = cue->id != NULL ? strlen(cue->id) : 0;
  if (id_length == 0 || !isfinite(cue->start_milliseconds) ||
      cue->start_milliseconds < 0 || !isfinite(cue->duration_milliseconds) ||
      cue->duration_milliseconds < 0 ||
      !isfinite(cue->start_milliseconds + cue->duration_milliseconds)) {
    qa_range_error("Invalid caption cue interval");
  }
}

void qa_caption_timeline_init(qa_caption_timeline *timeline, qa_seat_id seat,
                              qa_localization_catalog *localization) {
  if (!qa_same_seat(seat, localization->seat)) {
    qa_error("Caption localization belongs to a different seat");
  }
  memset(timeline, 0, sizeof *timeline);
  timeline->seat = seat;
  timeline->preferences = (qa_caption_preferences){true, true, true};
  timeline->localization = localization;
}

void qa_caption_timeline_destroy(qa_caption_timeline *timeline) {
  qa_caption_timeline_clear(timeline);
  free(timeline->cues);
  memset(timeline, 0, sizeof *timeline);
}

void qa_caption_timeline_clear(qa_caption_timeline *timeline) {
  for (size_t i = 0; i < timeline->cue_count; i++) {
    qa_caption_cue_clear(&timeline->cues[i]);
  }
  timeline->cue_count = 0;
}

static bool map_set_cue(qa_caption_timeline *timeline,
                        const qa_caption_cue *cue) {
  for (size_t i = 0; i < timeline->cue_count; i++) {
    if (strcmp(timeline->cues[i].id, cue->id) == 0) {
      qa_caption_cue copy;
      if (!cue_copy(cue, &copy)) return false;
      qa_caption_cue_clear(&timeline->cues[i]);
      timeline->cues[i] = copy; /* JS Map keeps the first position. */
      return true;
    }
  }
  if (timeline->cue_count == timeline->cue_capacity) {
    size_t grown =
        timeline->cue_capacity == 0 ? 8 : timeline->cue_capacity * 2;
    qa_caption_cue *cues =
        (qa_caption_cue *)realloc(timeline->cues, grown * sizeof *cues);
    if (cues == NULL) return false;
    timeline->cues = cues;
    timeline->cue_capacity = grown;
  }
  if (!cue_copy(cue, &timeline->cues[timeline->cue_count])) return false;
  timeline->cue_count++;
  return true;
}

bool qa_caption_timeline_replace(qa_caption_timeline *timeline,
                                 const qa_caption_cue *cues, size_t count) {
  for (size_t i = 0; i < count; i++) validate_cue(&cues[i]);
  qa_caption_timeline_clear(timeline);
  for (size_t i = 0; i < count; i++) {
    if (!map_set_cue(timeline, &cues[i])) return false;
  }
  return true;
}

bool qa_caption_timeline_add(qa_caption_timeline *timeline,
                             const qa_caption_cue *cue) {
  validate_cue(cue);
  return map_set_cue(timeline, cue);
}

bool qa_caption_timeline_remove(qa_caption_timeline *timeline, const char *id) {
  for (size_t i = 0; i < timeline->cue_count; i++) {
    if (strcmp(timeline->cues[i].id, id) != 0) continue;
    qa_caption_cue_clear(&timeline->cues[i]);
    memmove(&timeline->cues[i], &timeline->cues[i + 1],
            (timeline->cue_count - i - 1) * sizeof *timeline->cues);
    timeline->cue_count--;
    return true;
  }
  return false;
}

void qa_active_captions_free(qa_active_caption *captions, size_t count) {
  for (size_t i = 0; i < count; i++) {
    free(captions[i].localized_text);
    free(captions[i].localized_speaker);
  }
  free(captions);
}

bool qa_caption_timeline_active_at(qa_caption_timeline *timeline,
                                   double playback_time_milliseconds,
                                   qa_active_caption **out, size_t *out_count) {
  *out = NULL;
  *out_count = 0;
  if (!isfinite(playback_time_milliseconds)) {
    qa_range_error("Caption time must be finite");
  }
  qa_active_caption *active = NULL;
  size_t count = 0;
  size_t capacity = 0;
  for (size_t i = 0; i < timeline->cue_count; i++) {
    const qa_caption_cue *cue = &timeline->cues[i];
    if (!cue->kind_caption ? !timeline->preferences.subtitles
                           : !timeline->preferences.sound_captions) {
      continue;
    }
    if (playback_time_milliseconds < cue->start_milliseconds ||
        playback_time_milliseconds >=
            cue->start_milliseconds + cue->duration_milliseconds) {
      continue;
    }
    if (count == capacity) {
      size_t grown = capacity == 0 ? 4 : capacity * 2;
      qa_active_caption *next =
          (qa_active_caption *)realloc(active, grown * sizeof *next);
      if (next == NULL) {
        qa_active_captions_free(active, count);
        return false;
      }
      active = next;
      capacity = grown;
    }
    char *localized_text = qa_localization_table_localize(
        &timeline->localization->table, cue->text,
        (const char *const *)cue->arguments, cue->argument_count, true, 1024,
        NULL);
    char *localized_speaker = NULL;
    if (timeline->preferences.speakers && cue->speaker != NULL) {
      localized_speaker = qa_localization_table_localize(
          &timeline->localization->table, cue->speaker, NULL, 0, true, 1024,
          NULL);
    }
    if (localized_text == NULL ||
        (timeline->preferences.speakers && cue->speaker != NULL &&
         localized_speaker == NULL)) {
      free(localized_text);
      free(localized_speaker);
      qa_active_captions_free(active, count);
      return false;
    }
    active[count].cue = cue;
    active[count].localized_text = localized_text;
    active[count].localized_speaker = localized_speaker;
    count++;
  }
  /* Stable insertion sort by start, matching JS Array.sort. NaN starts
   * cannot occur (validated finite at insertion). */
  for (size_t i = 1; i < count; i++) {
    qa_active_caption hold = active[i];
    size_t j = i;
    while (j > 0 &&
           active[j - 1].cue->start_milliseconds > hold.cue->start_milliseconds) {
      active[j] = active[j - 1];
      j--;
    }
    active[j] = hold;
  }
  *out = active;
  *out_count = count;
  return true;
}

/* --- subtitle parsing --- */

static bool is_space(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' ||
         c == '\r';
}
static bool is_digit(char c) { return c >= '0' && c <= '9'; }

/* TS timestamp(value): /^(?:(\d+):)?(\d{2}):(\d{2})[.,](\d{3})$/ over
 * value.trim(). The optional hours group is tried first, as the regex does. */
static bool timestamp_tail(const char *cursor, const char *end, double *minutes,
                           double *seconds, double *milliseconds) {
  if (end - cursor != 9) return false;
  if (!is_digit(cursor[0]) || !is_digit(cursor[1]) || cursor[2] != ':' ||
      !is_digit(cursor[3]) || !is_digit(cursor[4]) ||
      (cursor[5] != '.' && cursor[5] != ',') || !is_digit(cursor[6]) ||
      !is_digit(cursor[7]) || !is_digit(cursor[8])) {
    return false;
  }
  *minutes = (cursor[0] - '0') * 10 + (cursor[1] - '0');
  *seconds = (cursor[3] - '0') * 10 + (cursor[4] - '0');
  *milliseconds = (cursor[6] - '0') * 100 + (cursor[7] - '0') * 10 + (cursor[8] - '0');
  return true;
}

static double timestamp_value(const char *value, size_t value_length) {
  const char *p = value;
  const char *end = value + value_length;
  while (p < end && is_space(*p)) p++;
  while (end > p && is_space(end[-1])) end--;
  double hours = 0, minutes = 0, seconds = 0, milliseconds = 0;
  const char *scan = p;
  double scanned = 0;
  while (scan < end && is_digit(*scan)) {
    scanned = scanned * 10 + (*scan - '0');
    scan++;
  }
  bool ok = false;
  if (scan > p && scan < end && *scan == ':' &&
      timestamp_tail(scan + 1, end, &minutes, &seconds, &milliseconds)) {
    hours = scanned;
    ok = true;
  } else if (timestamp_tail(p, end, &minutes, &seconds, &milliseconds)) {
    ok = true;
  }
  if (!ok || minutes >= 60 || seconds >= 60) {
    qa_error("Invalid subtitle timestamp %.*s", (int)value_length, value);
  }
  return ((hours * 60 + minutes) * 60 + seconds) * 1000 + milliseconds;
}

typedef struct text_buffer {
  char *data;
  size_t length;
  size_t capacity;
} text_buffer;
static bool buffer_add(text_buffer *buffer, const char *text, size_t length) {
  if (buffer->length + length + 1 > buffer->capacity) {
    size_t capacity = buffer->capacity == 0 ? 256 : buffer->capacity;
    while (capacity < buffer->length + length + 1) capacity *= 2;
    char *data = (char *)realloc(buffer->data, capacity);
    if (data == NULL) return false;
    buffer->data = data;
    buffer->capacity = capacity;
  }
  memcpy(buffer->data + buffer->length, text, length);
  buffer->length += length;
  buffer->data[buffer->length] = '\0';
  return true;
}

/* TS parseBlock. Every throw happens before anything is allocated, so a
 * raise abandons no memory. Returns false on OOM; *out has no id when the
 * block has no timing line (TS null). */
typedef struct parse_block_job {
  const char *source;
  size_t block_begin;
  size_t block_end;
  const char *name_space;
  size_t index;
  qa_caption_cue *out;
  bool ok;
} parse_block_job;

static void parse_block(void *raw) {
  parse_block_job *job = (parse_block_job *)raw;
  const char *source = job->source;
  size_t block_begin = job->block_begin, block_end = job->block_end;
  memset(job->out, 0, sizeof *job->out);
  job->ok = true;
  /* lines.findIndex(line => line.includes("-->")) */
  size_t timing_begin = 0, timing_end = 0, text_begin = 0;
  bool has_timing = false;
  size_t line_begin = block_begin;
  for (;;) {
    size_t line_end = line_begin;
    while (line_end < block_end && source[line_end] != '\n') line_end++;
    for (size_t i = line_begin; i + 3 <= line_end; i++) {
      if (memcmp(source + i, "-->", 3) == 0) {
        has_timing = true;
        timing_begin = line_begin;
        timing_end = line_end;
        text_begin = line_end < block_end ? line_end + 1 : block_end;
        break;
      }
    }
    if (has_timing || line_end >= block_end) break;
    line_begin = line_end + 1;
  }
  if (!has_timing) return;
  /* /^\s*(\S+)\s+-->\s+(\S+)/ on the timing line. */
  const char *line = source + timing_begin;
  size_t line_length = timing_end - timing_begin;
  size_t i = 0;
  while (i < line_length && is_space(line[i])) i++;
  size_t first_begin = i;
  while (i < line_length && !is_space(line[i])) i++;
  size_t first_end = i;
  size_t gap = i;
  while (i < line_length && is_space(line[i])) i++;
  bool matched = first_end > first_begin && i > gap && i + 3 <= line_length &&
                 memcmp(line + i, "-->", 3) == 0;
  size_t second_begin = 0, second_end = 0;
  if (matched) {
    i += 3;
    gap = i;
    while (i < line_length && is_space(line[i])) i++;
    second_begin = i;
    while (i < line_length && !is_space(line[i])) i++;
    second_end = i;
    matched = i > gap && second_end > second_begin;
  }
  if (!matched) qa_error("Invalid subtitle cue timing");
  double start_ms = timestamp_value(line + first_begin, first_end - first_begin);
  double end_ms = timestamp_value(line + second_begin, second_end - second_begin);
  /* validateCue: the id `${namespace}:${index}` is never empty. */
  qa_caption_cue probe;
  memset(&probe, 0, sizeof probe);
  probe.id = (char *)":";
  probe.start_milliseconds = start_ms;
  probe.duration_milliseconds = end_ms - start_ms;
  validate_cue(&probe);
  /* Cue text: remaining lines joined with \n, then trimEnd. */
  size_t text_end = block_end;
  while (text_end > text_begin && is_space(source[text_end - 1])) text_end--;
  char *cue_text = dup_range(source + text_begin, text_end - text_begin);
  if (cue_text == NULL) {
    job->ok = false;
    return;
  }
  /* Voice form /^<v\s+([^>]+)>([\s\S]*?)(?:<\/v>)?$/. */
  char *cue_speaker = NULL;
  if (cue_text[0] == '<' && cue_text[1] == 'v' && is_space(cue_text[2])) {
    const char *spaces = cue_text + 2;
    const char *speaker_begin = spaces;
    while (is_space(*speaker_begin)) speaker_begin++;
    const char *close = strchr(speaker_begin, '>');
    /* \s+ backtracks one whitespace into ([^>]+) when nothing else is left. */
    if (close != NULL && close == speaker_begin && speaker_begin - spaces >= 2) speaker_begin--;
    if (close != NULL && close > speaker_begin) {
      cue_speaker = dup_range(speaker_begin, (size_t)(close - speaker_begin));
      const char *rest = close + 1;
      size_t rest_length = strlen(rest);
      if (rest_length >= 4 && strcmp(rest + rest_length - 4, "</v>") == 0) rest_length -= 4;
      char *body = dup_range(rest, rest_length);
      if (cue_speaker == NULL || body == NULL) {
        free(cue_speaker);
        free(body);
        free(cue_text);
        job->ok = false;
        return;
      }
      free(cue_text);
      cue_text = body;
    }
  }
  char id[512];
  snprintf(id, sizeof id, "%s:%zu", job->name_space, job->index);
  qa_caption_cue *cue = job->out;
  cue->id = dup_text(id);
  cue->kind_caption = false;
  cue->start_milliseconds = start_ms;
  cue->duration_milliseconds = end_ms - start_ms;
  cue->text = cue_text;
  cue->speaker = cue_speaker;
  if (cue->id == NULL) {
    qa_caption_cue_clear(cue);
    job->ok = false;
  }
}

typedef struct parse_state {
  text_buffer normalized;
  qa_caption_cue *cues;
  size_t cue_count;
} parse_state;

static void parse_state_free(parse_state *state) {
  qa_caption_cues_free(state->cues, state->cue_count);
  free(state->normalized.data);
  memset(state, 0, sizeof *state);
}

bool qa_parse_subtitle_text(const char *text, size_t length, const char *name_space,
                            qa_subtitle_skipped_fn skipped, void *skipped_ctx,
                            qa_caption_cue **out, size_t *out_count) {
  *out = NULL;
  *out_count = 0;
  /* text.replace(/^﻿/, "") (one BOM, in its UTF-8 form) and
   * /\r\n?/g -> "\n". */
  const unsigned char *bytes = (const unsigned char *)text;
  size_t start = 0;
  if (length >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf) start = 3;
  parse_state state;
  memset(&state, 0, sizeof state);
  size_t cue_capacity = 0;
  for (size_t i = start; i < length; i++) {
    char c = (char)bytes[i];
    if (c == '\r') {
      if (!buffer_add(&state.normalized, "\n", 1)) goto oom;
      if (i + 1 < length && bytes[i + 1] == '\n') i++;
    } else if (!buffer_add(&state.normalized, &c, 1)) {
      goto oom;
    }
  }
  if (state.normalized.data == NULL && !buffer_add(&state.normalized, "", 0)) goto oom;
  const char *source = state.normalized.data;
  size_t source_length = state.normalized.length;
  size_t block_begin = 0;
  for (;;) {
    /* Split on /\n[ \t]*\n/: find the next block end. */
    size_t block_end = source_length;
    size_t next_begin = source_length;
    bool last = true;
    for (size_t i = block_begin; i < source_length; i++) {
      if (source[i] != '\n') continue;
      size_t j = i + 1;
      while (j < source_length && (source[j] == ' ' || source[j] == '\t')) j++;
      if (j < source_length && source[j] == '\n') {
        block_end = i;
        next_begin = j + 1;
        last = false;
        break;
      }
    }
    if (state.cue_count == cue_capacity) {
      size_t grown = cue_capacity == 0 ? 8 : cue_capacity * 2;
      qa_caption_cue *next = (qa_caption_cue *)realloc(state.cues, grown * sizeof *next);
      if (next == NULL) goto oom;
      state.cues = next;
      cue_capacity = grown;
    }
    parse_block_job job = {source, block_begin, block_end, name_space, state.cue_count,
                           &state.cues[state.cue_count], true};
    qa_caught_error caught;
    if (!qa_try(parse_block, &job, &caught)) {
      /* TS: rethrow unless a skipped callback is present (every raise here
       * is an Error). */
      if (skipped == NULL) {
        parse_state_free(&state);
        qa_rethrow(&caught);
      }
      size_t need = strlen(name_space) + strlen(caught.message) + 64;
      char *reason = (char *)malloc(need);
      if (reason == NULL) goto oom;
      snprintf(reason, need, "%s: skipped invalid subtitle cue (%s)", name_space, caught.message);
      skipped(skipped_ctx, reason);
      free(reason);
    } else if (!job.ok) {
      goto oom;
    } else if (job.out->id != NULL) {
      state.cue_count++;
    }
    if (last) break;
    block_begin = next_begin;
  }
  free(state.normalized.data);
  *out = state.cues;
  *out_count = state.cue_count;
  return true;
oom:
  parse_state_free(&state);
  return false;
}
