#ifndef QA_MEDIA_CAPTIONS_SAVE_H
#define QA_MEDIA_CAPTIONS_SAVE_H
#include "qa/media_captions.h"
#include "qa/caption_save.h"
/* The actual prepared view is borrowed and must outlive the media owner.
 * Compiled import qualifies the factory's seat/kind/localization tuple and
 * source path, and restores pool aliases without acquiring sidecars or invoking
 * callbacks. Factory aliases require the actual compiled resource pools.
 * NULL view/path are valid only for an unprepared owner. The caller checkpoints
 * and reconstructs that view through its genuine retained content graph. */
bool qa_media_captions_idle(const qa_media_captions *);
bool qa_media_captions_checkpoint(const qa_media_captions *, const qa_vfs *, const char *source, qa_buffer *, qa_error *);
bool qa_media_captions_restore(qa_media_captions *, qa_vfs *, const char *source, qa_bytes, qa_error *);
#endif
