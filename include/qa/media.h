#ifndef QA_MEDIA_H
#define QA_MEDIA_H

#include "qa/vfs.h"

typedef struct qa_media_input qa_media_input;
/* Immutable byte sources share storage or one retained file descriptor.
 * Callers serialize mutation of references; independent reads use offsets. */
bool qa_media_input_file(const char *path, qa_media_input **out, qa_error *);
bool qa_media_input_resource(qa_resource *, qa_media_input **out, qa_error *);
/* The lease transfers only on success. With no release callback, the caller
 * keeps bytes alive until the last input reference is released. */
bool qa_media_input_memory(qa_bytes, void *lease, void (*release)(void *),
                            qa_media_input **out, qa_error *);
void qa_media_input_retain(qa_media_input *);
void qa_media_input_release(qa_media_input *);
uint64_t qa_media_input_size(const qa_media_input *);
bool qa_media_input_read(qa_media_input *, uint64_t offset, void *destination,
                          size_t length, qa_error *);
bool qa_media_input_digest(qa_media_input *, qa_sha256_digest *out, qa_error *);

typedef struct qa_cin_asset qa_cin_asset;
typedef struct qa_cin_decoder qa_cin_decoder;
typedef struct qa_cin_info {
    uint32_t width, height, sample_rate;
    uint8_t channels, sample_bytes;
} qa_cin_info;
typedef struct qa_cin_frame {
    bool ended;
    uint64_t index, source_sample;
    const uint8_t *pixels, *palette;
    qa_bytes audio;
    qa_cin_info info;
} qa_cin_frame;
typedef struct qa_cin_checkpoint {
    qa_sha256_digest content;
    uint64_t input_size, offset, next_frame;
    uint8_t palette[768];
    bool ended;
} qa_cin_checkpoint;
/* Codebooks and source are shared by independent decoders. */
bool qa_cin_asset_load(qa_media_input *, qa_cin_asset **out, qa_error *);
void qa_cin_asset_retain(qa_cin_asset *);
void qa_cin_asset_release(qa_cin_asset *);
qa_cin_info qa_cin_asset_info(const qa_cin_asset *);
bool qa_cin_decoder_create(qa_cin_asset *, qa_cin_decoder **out, qa_error *);
void qa_cin_decoder_destroy(qa_cin_decoder *);
/* Frame spans are borrowed until the next decode or rewind. PCM bytes retain
 * source encoding: unsigned 8-bit or signed little-endian 16-bit. Failure does
 * not advance the stream; previously borrowed pixel/audio scratch may change. */
bool qa_cin_decoder_next(qa_cin_decoder *, qa_cin_frame *out, qa_error *);
void qa_cin_decoder_rewind(qa_cin_decoder *);
const uint8_t *qa_cin_decoder_palette(const qa_cin_decoder *);
uint64_t qa_cin_decoder_index(const qa_cin_decoder *);
bool qa_cin_decoder_capture(qa_cin_decoder *, qa_cin_checkpoint *, qa_error *);
bool qa_cin_decoder_restore(qa_cin_decoder *, const qa_cin_checkpoint *, qa_error *);
bool qa_cin_sample_range(uint64_t frame, uint32_t rate, uint64_t *start, uint64_t *end, qa_error *);
/* RGBA output permits the Q2 playback owner to display retained old indices
 * with the palette updated by a prefetched frame. */
bool qa_cin_rgba(qa_bytes pixels, qa_bytes palette, void *rgba, size_t capacity, qa_error *);

typedef enum qa_media_status {
    QA_MEDIA_PLAYING, QA_MEDIA_PAUSED, QA_MEDIA_HELD, QA_MEDIA_ENDED, QA_MEDIA_STOPPED
} qa_media_status;
typedef struct qa_media_frame {
    qa_bytes rgba;
    uint32_t width, height;
    uint64_t index, loop;
    double source_ms, presentation_ms;
} qa_media_frame;
typedef struct qa_media_audio {
    qa_bytes pcm;
    uint32_t rate;
    uint8_t channels, sample_bytes;
    uint64_t source_sample, loop;
    double source_ms, presentation_ms;
    bool reset;
} qa_media_audio;
typedef struct qa_media_tick {
    qa_media_status status;
    const qa_media_frame *frame;
    bool changed, looped;
} qa_media_tick;
typedef struct qa_cin_playback qa_cin_playback;
typedef struct qa_cin_playback_options {
    bool loop, hold, silent;
    void *context;
    /* Callbacks borrow payloads and may enqueue output. They must not mutate
     * this playback. A callback failure faults this playback until restart. */
    bool (*audio)(void *, const qa_media_audio *, qa_error *);
    void (*dropped_frame)(void *, uint64_t requested, uint64_t decoded);
} qa_cin_playback_options;
typedef struct qa_cin_picture_checkpoint {
    qa_buffer pixels;
    uint64_t index;
    bool present;
} qa_cin_picture_checkpoint;
typedef struct qa_cin_playback_checkpoint {
    qa_cin_checkpoint decoder;
    qa_cin_picture_checkpoint picture, pending;
    double epoch_ms;
    uint64_t loop;
    qa_media_status status;
    bool repeat, hold, silent;
} qa_cin_playback_checkpoint;
bool qa_cin_playback_create(qa_cin_asset *, const qa_cin_playback_options *, double now_ms,
                            qa_cin_playback **out, qa_error *);
void qa_cin_playback_destroy(qa_cin_playback *);
bool qa_cin_playback_restart(qa_cin_playback *, double now_ms, qa_error *);
bool qa_cin_playback_tick(qa_cin_playback *, double now_ms, bool game_focus,
                          qa_media_tick *out, qa_error *);
/* Borrowed until the next playback operation. */
const qa_media_frame *qa_cin_playback_frame(qa_cin_playback *);
bool qa_cin_playback_capture(qa_cin_playback *, qa_cin_playback_checkpoint *, qa_error *);
void qa_cin_playback_checkpoint_free(qa_cin_playback_checkpoint *);
bool qa_cin_playback_restore(qa_cin_playback *, const qa_cin_playback_checkpoint *, qa_error *);

#endif
