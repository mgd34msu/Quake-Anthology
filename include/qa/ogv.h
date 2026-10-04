#ifndef QA_OGV_H
#define QA_OGV_H

#include "qa/media.h"

typedef struct qa_ogv_asset qa_ogv_asset;
typedef struct qa_ogv_playback qa_ogv_playback;
typedef struct qa_ogv_info {
    uint32_t width, height;
    uint64_t frames;
    double frame_ms;
    bool has_audio;
} qa_ogv_info;
typedef struct qa_ogv_options {
    bool loop, hold, silent;
    void *context;
    /* PCM is borrowed for the callback; do not mutate or destroy its playback. */
    bool (*audio)(void *, const qa_media_audio *, qa_error *);
} qa_ogv_options;
typedef struct qa_ogv_checkpoint {
    double epoch_ms;
    uint64_t loop, next_index, audio_position;
    qa_media_status status;
    qa_media_frame frame;
    qa_buffer pixels;
    bool has_frame, has_audio, reset_audio, repeat, hold, silent;
} qa_ogv_checkpoint;

/* Loading borrows input only for this call. The asset owns shared compressed
 * packets, soundtrack bytes, and parsed Theora headers. Serialize retain/release;
 * independent playbacks have private native contexts, cursors, and outputs. */
bool qa_ogv_asset_load(qa_media_input *, qa_ogv_asset **out, qa_error *);
void qa_ogv_asset_retain(qa_ogv_asset *);
void qa_ogv_asset_release(qa_ogv_asset *);
qa_ogv_info qa_ogv_asset_info(const qa_ogv_asset *);
bool qa_ogv_playback_create(qa_ogv_asset *, const qa_ogv_options *, double now_ms,
                            qa_ogv_playback **out, qa_error *);
/* Destruction during an audio callback is rejected without changing playback. */
void qa_ogv_playback_destroy(qa_ogv_playback *);
/* Decoder or audio callback failures require restart or restore before ticking. */
bool qa_ogv_playback_tick(qa_ogv_playback *, double now_ms, qa_media_tick *, qa_error *);
bool qa_ogv_playback_restart(qa_ogv_playback *, double now_ms, qa_error *);
/* The frame and its pixels are borrowed until the next playback mutation. */
const qa_media_frame *qa_ogv_playback_frame(const qa_ogv_playback *);
/* Capture writes a new owned checkpoint. Free its pixels before reusing out.
 * frame.rgba aliases pixels; restore validates pixels and ignores that pointer.
 * Restore emits no callbacks and leaves playback unchanged on failure. */
bool qa_ogv_playback_capture(qa_ogv_playback *, qa_ogv_checkpoint *, qa_error *);
bool qa_ogv_playback_restore(qa_ogv_playback *, const qa_ogv_checkpoint *, qa_error *);
void qa_ogv_checkpoint_free(qa_ogv_checkpoint *);

#endif
