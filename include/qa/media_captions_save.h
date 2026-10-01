#ifndef QA_MEDIA_CAPTIONS_SAVE_H
#define QA_MEDIA_CAPTIONS_SAVE_H
#include "qa/media_captions.h"
#include "qa/caption_save.h"
#include "qa/audio_bank_graph_save.h"
/* The actual prepared view is borrowed and must outlive the media owner.
 * Compiled import qualifies the factory's seat/kind/localization tuple and
 * source path, and restores pool aliases without acquiring sidecars or invoking
 * callbacks. Factory aliases require the actual compiled resource pools.
 * NULL view/path are valid only for an unprepared owner. The caller checkpoints
 * and reconstructs that view through its genuine retained content graph. */
bool qa_media_captions_idle(const qa_media_captions *);
bool qa_media_captions_checkpoint(const qa_media_captions *, const qa_vfs *, const char *source, qa_buffer *, qa_error *);
bool qa_media_captions_restore(qa_media_captions *, qa_vfs *, const char *source, qa_bytes, qa_error *);
typedef struct qa_sound_caption_save_refs {
    const qa_audio_asset_inventory *assets;
    void *context;
    bool (*view_key)(void *, const qa_vfs *, uint64_t *, qa_error *);
    bool (*claim_view)(void *, uint64_t, qa_vfs **, qa_error *);
} qa_sound_caption_save_refs;
/* Import consumes only compiled pool rows, actual asset aliases and detached
 * content view owners. No observer, sidecar acquisition or preparation runs. */
bool qa_sound_captions_checkpoint(const qa_sound_captions *, const qa_sound_caption_save_refs *, qa_buffer *, qa_error *);
bool qa_sound_captions_restore(qa_sound_captions *, const qa_sound_caption_save_refs *, qa_bytes, qa_error *);
#endif
