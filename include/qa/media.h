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
    /* Native signed 16-bit from RoQ/OGV; CIN retains little-endian 16-bit
     * or unsigned 8-bit source bytes. */
    bool reset, native_pcm;
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
bool qa_cin_playback_create(qa_cin_asset *, const qa_cin_playback_options *, double now_ms,
                            qa_cin_playback **out, qa_error *);
void qa_cin_playback_destroy(qa_cin_playback *);
bool qa_cin_playback_restart(qa_cin_playback *, double now_ms, qa_error *);
bool qa_cin_playback_tick(qa_cin_playback *, double now_ms, bool game_focus,
                          qa_media_tick *out, qa_error *);
/* Borrowed until the next playback operation. */
const qa_media_frame *qa_cin_playback_frame(qa_cin_playback *);

typedef enum qa_roq_book_mode { QA_ROQ_BOOK_NORMAL, QA_ROQ_BOOK_HALF, QA_ROQ_BOOK_DOUBLE } qa_roq_book_mode;
/* Zero initialize per movie. All pixel profiles share the same backing bytes,
 * preserving partial codebook updates and mode changes without type punning. */
typedef struct qa_roq_codebooks {
    uint8_t book2[256 * 8 * 4], book4[256 * 32 * 4], book8[256 * 128 * 4];
} qa_roq_codebooks;
uint16_t qa_roq_yuv565(uint8_t y, uint8_t u, uint8_t v);
uint32_t qa_roq_yuv_rgba(uint8_t y, uint8_t u, uint8_t v);
bool qa_roq_codebook_decode(qa_roq_codebooks *, qa_bytes bytes, uint16_t flags,
                            qa_roq_book_mode, unsigned bytes_per_pixel, qa_bytes gray,
                            bool diagnostic_two_only, size_t *consumed, qa_error *);
typedef enum qa_roq_audio_mode {
    QA_ROQ_MONO_TO_MONO, QA_ROQ_MONO_TO_STEREO, QA_ROQ_STEREO_TO_STEREO, QA_ROQ_STEREO_TO_MONO
} qa_roq_audio_mode;
/* Source size means input bytes except STEREO_TO_MONO, where it means output
 * mono samples. Output is native signed PCM; returned frames exclude channels. */
bool qa_roq_audio_decode(qa_bytes input, size_t source_size, uint16_t flags,
                          qa_roq_audio_mode, bool signed_output, int16_t *output,
                          size_t sample_capacity, size_t *frames, qa_error *);

typedef struct qa_roq_scratch qa_roq_scratch;
typedef struct qa_roq_decoder qa_roq_decoder;
typedef enum qa_roq_end_policy { QA_ROQ_COMPLETE, QA_ROQ_CINEMATIC } qa_roq_end_policy;
typedef enum qa_roq_event_kind { QA_ROQ_END, QA_ROQ_INFO, QA_ROQ_METADATA, QA_ROQ_FRAME, QA_ROQ_AUDIO } qa_roq_event_kind;
typedef struct qa_roq_event {
    qa_roq_event_kind kind;
    union {
        struct { uint32_t width, height; } info;
        struct { qa_bytes rgba; uint64_t index; double time_ms; size_t physical_offset; } frame;
        struct { const int16_t *samples; size_t frames; uint8_t channels; } audio;
    } data;
} qa_roq_event;
typedef struct qa_roq_decoder_options {
    qa_roq_end_policy end_policy;
    bool silent;
    /* NULL creates private scratch. Sharing is explicit for original Q3
     * cinematic handles, whose owner serializes decoding and rendering. */
    qa_roq_scratch *scratch;
} qa_roq_decoder_options;
typedef struct qa_roq_decode_hooks {
    void *context;
    bool (*before_stereo)(void *, qa_error *);
    bool (*info)(void *, uint32_t width, uint32_t height, qa_error *);
    bool (*audio)(void *, const qa_roq_event *, qa_error *);
} qa_roq_decode_hooks;
bool qa_roq_scratch_create(qa_roq_scratch **out, qa_error *);
void qa_roq_scratch_retain(qa_roq_scratch *);
void qa_roq_scratch_release(qa_roq_scratch *);
void qa_roq_scratch_clear(qa_roq_scratch *, bool clear_codebooks);
/* The cinematic pool serializes all users before saving or replacing these
 * physical bytes. Reference counts and decoder cursors are separate owners. */
bool qa_roq_scratch_capture(const qa_roq_scratch *, qa_buffer *, qa_error *);
bool qa_roq_scratch_restore(qa_roq_scratch *, qa_bytes, qa_error *);
bool qa_roq_decoder_create(qa_media_input *, const qa_roq_decoder_options *, qa_roq_decoder **out, qa_error *);
void qa_roq_decoder_destroy(qa_roq_decoder *);
/* Borrowed payloads remain valid until the next decoder operation using the
 * same scratch. Callbacks run before the next chunk is selected and must not
 * mutate this decoder. A failed dispatch requires rewind or restore. */
bool qa_roq_decoder_chunk(qa_roq_decoder *, const qa_roq_decode_hooks *, qa_roq_event *, qa_error *);
bool qa_roq_decoder_next(qa_roq_decoder *, qa_roq_event *, qa_error *);
bool qa_roq_decoder_rewind(qa_roq_decoder *, qa_error *);
bool qa_roq_decoder_scratch_rebind_ready(const qa_roq_decoder *, const qa_roq_scratch *, qa_error *);
void qa_roq_decoder_scratch_rebind(qa_roq_decoder *, qa_roq_scratch *);
uint16_t qa_roq_decoder_rate(const qa_roq_decoder *);
void qa_roq_decoder_dimensions(const qa_roq_decoder *, uint32_t *width, uint32_t *height);
bool qa_roq_decoder_in_packet(const qa_roq_decoder *);
bool qa_roq_decoder_invalid(const qa_roq_decoder *);
bool qa_roq_decoder_reset_after_run(const qa_roq_decoder *);
/* Original uploads can address the retained physical image allocation beyond
 * the currently published frame. The returned range is always bounds checked. */
bool qa_roq_decoder_view(const qa_roq_decoder *, size_t offset, size_t length, qa_bytes *, qa_error *);

typedef struct qa_media_clock {
    void *context;
    double (*sample)(void *);
} qa_media_clock;
typedef struct qa_roq_playback qa_roq_playback;
typedef struct qa_roq_playback_options {
    bool loop, hold, silent, shader;
    qa_roq_scratch *scratch;
    void *context;
    bool (*audio)(void *, const qa_media_audio *, qa_error *);
    bool (*before_audio_reset)(void *, qa_error *);
    bool (*info)(void *, uint32_t width, uint32_t height, qa_error *);
    bool (*frame)(void *, const qa_media_frame *, size_t physical_offset, qa_error *);
    void (*diagnostic)(void *, const char *);
} qa_roq_playback_options;
bool qa_roq_playback_create(qa_media_input *, const qa_roq_playback_options *, qa_media_clock,
                            qa_roq_playback **out, qa_error *);
void qa_roq_playback_destroy(qa_roq_playback *);
bool qa_roq_playback_tick(qa_roq_playback *, qa_media_clock, qa_media_tick *, qa_error *);
/* Full reset clears retained image/codebook state; restart preserves it. */
bool qa_roq_playback_restart(qa_roq_playback *, qa_media_clock, bool full_reset, qa_error *);
bool qa_roq_playback_scratch_rebind_ready(const qa_roq_playback *, const qa_roq_scratch *, qa_error *);
void qa_roq_playback_scratch_rebind(qa_roq_playback *, qa_roq_scratch *);
const qa_media_frame *qa_roq_playback_frame(const qa_roq_playback *);
/* Shader and UI uploads retain the original physical-buffer sampling rules.
 * The image is borrowed until the next playback operation. */
bool qa_roq_playback_image(qa_roq_playback *, bool shader, uint32_t draw_width,
                           uint32_t draw_height, bool dirty, qa_media_frame *, qa_error *);

#endif
