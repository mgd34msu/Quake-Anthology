/*
 * Port of src/text/captions.ts: seat-bound subtitle/caption cue
 * timelines and the SRT/WebVTT sidecar parser.
 * C accommodations:
 *  - LocalizationCatalog is qa_localization_catalog (text/
 *    localization.h), borrowed by the timeline; the seat check uses
 *    qa_same_seat and aborts with the TS Error message.
 *  - Cues own malloc'd id/text/speaker/arguments. The timeline deep
 *    copies cues on add/replace (the TS Map stores the given frozen
 *    objects). activeAt returns an owned array whose localized strings
 *    are malloc'd; free with qa_active_captions_free. localize is
 *    called with the TS defaults (in-place allowed, 1024 bytes).
 *  - parseSubtitleText takes bytes + length (UTF-8 where the TS takes
 *    a UTF-16 string; ASCII structure is identical). Its regex
 *    whitespace classes cover ASCII whitespace; JS trimEnd on cue
 *    text likewise. Invalid timestamps/timings abort with the TS
 *    Error messages; invalid cue intervals abort as RangeError.
 * GPL-2.0-or-later.
 */
#ifndef QA_TEXT_CAPTIONS_H
#define QA_TEXT_CAPTIONS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/identity.h"
#include "qa/text/localization.h"

/* TS CaptionCue. kind_caption false is "subtitle", true "caption".
 * speaker NULL is TS null. In a cue list every string is owned. */
typedef struct qa_caption_cue {
  char *id;
  bool kind_caption;
  double start_milliseconds;
  double duration_milliseconds;
  char *text;
  char *speaker;
  char **arguments;
  size_t argument_count;
} qa_caption_cue;

/* TS ActiveCaption: the cue fields are borrowed from the timeline;
 * localized_text/localized_speaker are owned by the result array. */
typedef struct qa_active_caption {
  const qa_caption_cue *cue;
  char *localized_text;
  char *localized_speaker; /* NULL is TS null */
} qa_active_caption;

/* TS CaptionPreferences. */
typedef struct qa_caption_preferences {
  bool subtitles;
  bool sound_captions;
  bool speakers;
} qa_caption_preferences;

/* TS CaptionTimeline. preferences is public, as in the TS. */
typedef struct qa_caption_timeline {
  qa_seat_id seat;
  qa_caption_preferences preferences;
  qa_localization_catalog *localization; /* borrowed */
  qa_caption_cue *cues;                  /* owned; TS Map order */
  size_t cue_count;
  size_t cue_capacity;
} qa_caption_timeline;

/* Aborts when the catalog belongs to another seat (TS Error). */
void qa_caption_timeline_init(qa_caption_timeline *timeline, qa_seat_id seat,
                              qa_localization_catalog *localization);
void qa_caption_timeline_destroy(qa_caption_timeline *timeline);
/* Validates every cue first, as the TS does. False only on OOM. */
bool qa_caption_timeline_replace(qa_caption_timeline *timeline,
                                 const qa_caption_cue *cues, size_t count);
bool qa_caption_timeline_add(qa_caption_timeline *timeline,
                             const qa_caption_cue *cue);
bool qa_caption_timeline_remove(qa_caption_timeline *timeline, const char *id);
void qa_caption_timeline_clear(qa_caption_timeline *timeline);
/* Malloc'd actives sorted by start (stable, as JS sort is). False
 * only on OOM. Free with qa_active_captions_free. */
bool qa_caption_timeline_active_at(qa_caption_timeline *timeline,
                                   double playback_time_milliseconds,
                                   qa_active_caption **out, size_t *out_count);
void qa_active_captions_free(qa_active_caption *captions, size_t count);

/* Frees the owned strings of one cue / a cue array from the parser. */
void qa_caption_cue_clear(qa_caption_cue *cue);
void qa_caption_cues_free(qa_caption_cue *cues, size_t count);

/* TS parseSubtitleText. False only on OOM. */
bool qa_parse_subtitle_text(const char *text, size_t length,
                            const char *name_space, qa_caption_cue **out,
                            size_t *out_count);

#endif
