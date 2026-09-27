#ifndef QA_CAPTIONS_H
#define QA_CAPTIONS_H

#include "qa/localization.h"

typedef enum qa_caption_kind {
  QA_CAPTION_SUBTITLE,
  QA_CAPTION_SOUND
} qa_caption_kind;
typedef struct qa_caption_cue {
  const char *id, *text, *speaker;
  const char *const *arguments;
  size_t argument_count;
  double start_ms, duration_ms;
  qa_caption_kind kind;
} qa_caption_cue;
typedef struct qa_caption_preferences {
  bool subtitles, sound_captions, speakers;
} qa_caption_preferences;
typedef struct qa_active_caption {
  const qa_caption_cue *cue;
  const char *text, *speaker;
} qa_active_caption;
typedef struct qa_caption_track qa_caption_track;
typedef struct qa_captions qa_captions;
typedef struct qa_caption_library qa_caption_library;

/* Immutable cue tracks retain copied text, Unicode, overlap and source order.
 * Duplicate IDs replace values while retaining the first insertion position. */
bool qa_caption_track_create(const qa_caption_cue *, size_t,
                             qa_caption_track **, qa_error *);
bool qa_caption_track_parse(qa_bytes, const char *namespace_name,
                            qa_caption_kind, qa_caption_track **, qa_error *);
void qa_caption_track_retain(qa_caption_track *);
void qa_caption_track_release(qa_caption_track *);
size_t qa_caption_track_count(const qa_caption_track *);
const qa_caption_cue *qa_caption_track_at(const qa_caption_track *, size_t);

/* A seat binds shared immutable tracks/catalogs; its live cue additions and
 * preferences stay private. Readers receive temporary localized text and may
 * not mutate/destroy this timeline until visit returns. */
qa_captions *qa_captions_create(uint32_t seat, qa_localization *, qa_error *);
void qa_captions_destroy(qa_captions *);
void qa_captions_localization(qa_captions *, qa_localization *);
bool qa_captions_replace(qa_captions *, qa_caption_track *, qa_error *);
bool qa_captions_add(qa_captions *, const qa_caption_cue *, qa_error *);
bool qa_captions_remove(qa_captions *, const char *id);
void qa_captions_clear(qa_captions *);
uint32_t qa_captions_seat(const qa_captions *);
bool qa_captions_visit(qa_captions *, double time_ms, qa_caption_preferences,
                       void (*visit)(void *, const qa_active_caption *), void *,
                       qa_error *);

qa_caption_library *qa_caption_library_create(qa_error *);
void qa_caption_library_destroy(qa_caption_library *);
void qa_caption_library_trim(qa_caption_library *);
/* Resolve language-specific SRT, VTT, then unsuffixed SRT/VTT through this VFS.
 * No sidecar is a successful NULL track. Consumers replace even on a miss, so
 * captions from a previous movie/language cannot persist. */
bool qa_caption_library_load(qa_caption_library *, qa_vfs *, const char *movie,
                             const char *language, qa_caption_kind,
                             qa_caption_track **, qa_error *);

#endif
