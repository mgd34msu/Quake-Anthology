/*
 * Port of src/text/media-captions.ts: localized subtitle sidecar
 * lookup for seat media playback (SeatMediaCaptions).
 * C accommodations:
 *  - Async prepare() is synchronous (the read service returns bytes
 *    immediately), so the request counter never races; it is kept for
 *    parity. The live-language path still returns no captions for the
 *    call that notices a language change, as the TS does while its
 *    load is in flight. The failed callback is retained for API
 *    parity: load errors abort in C (see docs/PORTING.md), so it is
 *    never invoked.
 *  - CinematicStatus (media/types.ts) is the exact TS string:
 *    "playing" | "paused" | "held" | "ended" | "stopped".
 *  - A NULL localization at init makes prepare load one through
 *    text/localization-resources.h (q1-rerelease profile, the TS
 *    default); loaded catalogs are owned by the captions object, a
 *    supplied one is borrowed.
 * GPL-2.0-or-later.
 */
#ifndef QA_TEXT_MEDIA_CAPTIONS_H
#define QA_TEXT_MEDIA_CAPTIONS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/identity.h"
#include "qa/text/captions.h"
#include "qa/text/localization.h"
#include "qa/text/localization_resources.h"

/* TS subtitlePaths. *out is a malloc'd array of malloc'd paths; free
 * with qa_subtitle_paths_free. False only on OOM. */
bool qa_subtitle_paths(const char *source, const char *language, char ***out,
                       size_t *out_count);
void qa_subtitle_paths_free(char **paths, size_t count);

/* TS CaptionPlaybackState. */
typedef struct qa_caption_playback_state {
  const char *source;
  double source_time_milliseconds;
  const char *status; /* CinematicStatus string */
} qa_caption_playback_state;

/* TS liveLanguage. read returns a borrowed language string. */
typedef struct qa_live_language {
  void *ctx;
  const char *(*read)(void *ctx);
  void (*failed)(void *ctx); /* never invoked; see header note */
} qa_live_language;

/* TS SeatMediaCaptions. */
typedef struct qa_seat_media_captions {
  qa_seat_id seat;
  qa_localization_read_fn read;
  void *read_ctx;
  qa_localization_catalog *supplied; /* borrowed, may be NULL */
  bool kind_caption;
  bool has_live_language;
  qa_live_language live_language;
  qa_caption_timeline timeline;
  qa_localization_catalog *owned_catalog; /* timeline's, when owned */
  char *prepared;                         /* owned; "" when none */
  char *live_request;                     /* owned */
  int64_t request;
} qa_seat_media_captions;

/* localization NULL and live_language NULL mirror the TS defaults. */
void qa_seat_media_captions_init(qa_seat_media_captions *captions,
                                 qa_seat_id seat,
                                 qa_localization_read_fn read, void *read_ctx,
                                 qa_localization_catalog *localization,
                                 bool kind_caption,
                                 const qa_live_language *live_language);
void qa_seat_media_captions_destroy(qa_seat_media_captions *captions);
/* TS prepare (synchronous form). False only on OOM. */
bool qa_seat_media_captions_prepare(qa_seat_media_captions *captions,
                                    const char *source, const char *language);
/* TS active. Output like qa_caption_timeline_active_at. */
bool qa_seat_media_captions_active(qa_seat_media_captions *captions,
                                   qa_caption_playback_state state,
                                   qa_caption_preferences preferences,
                                   qa_active_caption **out, size_t *out_count);
/* TS clear. */
void qa_seat_media_captions_clear(qa_seat_media_captions *captions);

#endif
