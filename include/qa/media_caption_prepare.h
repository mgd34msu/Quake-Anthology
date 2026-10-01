#ifndef QA_MEDIA_CAPTION_PREPARE_H
#define QA_MEDIA_CAPTION_PREPARE_H
#include "qa/media_captions.h"

typedef struct qa_sound_caption_language qa_sound_caption_language;
/* Retains the actual sound owner while preparing a detached continuation of
 * its source views, compiled timelines and real voice clocks. Resource pools
 * and the parent outlive the ticket. No active captions change on refusal. */
bool qa_sound_caption_language_prepare(qa_sound_captions *,const char *language,
    int64_t delivered_frame,qa_sound_caption_language **,qa_error *);
bool qa_sound_caption_language_ready(const qa_sound_caption_language *,qa_error *);
/* The admitted pointer transfer invokes no callbacks and keeps both old and
 * new owners alive until finish. Finish releases the old continuation. */
void qa_sound_caption_language_publish(qa_sound_caption_language *);
bool qa_sound_caption_language_finish(qa_sound_caption_language **,qa_error *);
bool qa_sound_caption_language_abort(qa_sound_caption_language **,qa_error *);
#endif
