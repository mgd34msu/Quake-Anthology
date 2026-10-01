#ifndef QA_CAPTION_SAVE_H
#define QA_CAPTION_SAVE_H
#include "qa/captions.h"
#include "qa/source_save.h"
/* Compiled immutable catalogs/tracks and cache insertion order are imported
 * directly. No sidecar parser, localization parser, VFS acquisition or visit
 * callback executes. Consumer aliases resolve through these actual pools;
 * private tracks from add/replace retain their own compiled records and aliases. */
bool qa_caption_library_checkpoint(const qa_caption_library *, qa_buffer *, qa_error *);
bool qa_caption_library_restore(qa_caption_library *, qa_bytes, qa_error *);
bool qa_caption_library_track_key(const qa_caption_library *, const qa_caption_track *, uint64_t *);
qa_caption_track *qa_caption_library_track(const qa_caption_library *, uint64_t);
bool qa_localization_pool_checkpoint(const qa_localization_pool *, qa_buffer *, qa_error *);
bool qa_localization_pool_restore(qa_localization_pool *, qa_bytes, qa_error *);
bool qa_localization_pool_catalog_key(const qa_localization_pool *, const qa_localization *, uint64_t *);
qa_localization *qa_localization_pool_catalog(const qa_localization_pool *, uint64_t);
bool qa_captions_checkpoint(const qa_captions *, const qa_caption_library *, const qa_localization_pool *, qa_buffer *, qa_error *);
bool qa_captions_restore(qa_captions *, const qa_caption_library *, const qa_localization_pool *, qa_bytes, qa_error *);
#endif
