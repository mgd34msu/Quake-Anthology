#ifndef QA_MEDIA_CAPTIONS_H
#define QA_MEDIA_CAPTIONS_H

#include "qa/audio.h"
#include "qa/captions.h"
#include "qa/media.h"

typedef struct qa_media_captions qa_media_captions;
typedef struct qa_media_caption_options {
    uint32_t seat;
    qa_caption_kind kind;
    qa_caption_library *tracks;
    qa_localization_pool *catalogs;
    qa_localization *override_catalog;
    qa_localization_options localization;
} qa_media_caption_options;
/* Pools and diagnostic context outlive this owner; the optional catalog is
 * retained. Prepare before presentation; resource resolution never runs in
 * visit. */
qa_media_captions *qa_media_captions_create(const qa_media_caption_options *, qa_error *);
void qa_media_captions_destroy(qa_media_captions *);
void qa_media_captions_clear(qa_media_captions *);
/* Pure exact qualification; NULL tuple denotes an unprepared owner. */
bool qa_media_captions_prepared_is(const qa_media_captions *,const qa_vfs *,const char *source,const char *language);
bool qa_media_captions_prepare(qa_media_captions *, qa_vfs *, const char *source,
                               const char *language, qa_error *);
bool qa_media_captions_visit(qa_media_captions *, const char *source, double source_time_ms,
                             qa_media_status, qa_caption_preferences,
                             void (*visit)(void *, const qa_active_caption *), void *, qa_error *);

typedef struct qa_sound_captions qa_sound_captions;
typedef struct qa_sound_caption_options {
    qa_media_caption_options captions;
    size_t voice_capacity; /* Load-sized; zero selects 1024 records. */
    void *context;
    /* Resolve the sound's original content view, not the currently viewed map.
     * The returned view is borrowed for the preparation call. */
    bool (*content_view)(void *, qa_audio_asset *, qa_vfs **, qa_error *);
} qa_sound_caption_options;
qa_sound_captions *qa_sound_captions_create(const qa_sound_caption_options *, qa_error *);
void qa_sound_captions_destroy(qa_sound_captions *);
/* Warm the same sound catalog and timeline during content loading. */
bool qa_sound_captions_prepare_asset(qa_sound_captions *, qa_audio_asset *, const char *language, qa_error *);
/* Forward mixer notifications synchronously on the owning thread. Only source
 * leases and event state are retained here; loading is deferred to prepare. */
bool qa_sound_captions_event(qa_sound_captions *, const qa_audio_voice_event *, qa_error *);
bool qa_sound_captions_prepare(qa_sound_captions *, const char *language, int64_t delivered_frame,
                               qa_error *);
bool qa_sound_captions_visit(qa_sound_captions *, int64_t delivered_frame, qa_caption_preferences,
                             void (*visit)(void *, const qa_active_caption *), void *, qa_error *);
void qa_sound_captions_clear(qa_sound_captions *);
bool qa_sound_captions_idle(const qa_sound_captions *);
/* One borrowed entry for each actual owned asset reference, including stopped
 * voices and prepared caches. Caller frees only the pointer array. */
bool qa_sound_captions_assets_read(const qa_sound_captions *, qa_audio_asset ***, size_t *, qa_error *);
bool qa_sound_captions_views_visit(const qa_sound_captions *,
    bool (*)(void *, const qa_vfs *, qa_error *), void *, qa_error *);

#endif
