#ifndef QA_CAPTION_SAVE_H
#define QA_CAPTION_SAVE_H
#include "qa/captions.h"
#include "qa/source_save.h"
/* Cache keys qualify the actual timeline copy used for language changes. */
bool qa_caption_library_track_key(const qa_caption_library *, const qa_caption_track *, uint64_t *);
qa_caption_track *qa_caption_library_track(const qa_caption_library *, uint64_t);
bool qa_localization_pool_catalog_key(const qa_localization_pool *, const qa_localization *, uint64_t *);
qa_localization *qa_localization_pool_catalog(const qa_localization_pool *, uint64_t);
bool qa_captions_checkpoint(const qa_captions *, const qa_caption_library *, const qa_localization_pool *, qa_buffer *, qa_error *);
bool qa_captions_restore(qa_captions *, const qa_caption_library *, const qa_localization_pool *, qa_bytes, qa_error *);
#endif
