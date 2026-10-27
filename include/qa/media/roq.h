/*
 * Port of src/media/roq.ts (id Software cl_cin.c RoQ decoding):
 * the process-wide codebooks and fixed linbuf scratch, plus the
 * per-stream decoder. C accommodations: events own their frame /
 * sample buffers (dispose frees them); the three nextChunk hooks
 * are function pointers plus ctx; a decoder built from bytes owns
 * its stream, one built from a stream borrows it. A supplied
 * scratch is borrowed, otherwise the decoder owns one.
 * GPL-2.0-or-later.
 */
#ifndef QA_MEDIA_ROQ_H
#define QA_MEDIA_ROQ_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/media/roq_audio.h"
#include "qa/media/roq_codebook.h"
#include "qa/media/roq_stream.h"
#include "qa/persistence/value.h"

#define QA_ROQ_SCRATCH_FILE_LENGTH (65536 + 8)
#define QA_ROQ_SCRATCH_FRAMES_LENGTH (512 * 512 * 4 * 2)

typedef struct qa_roq_scratch {
  uint8_t *file;   /* owned, QA_ROQ_SCRATCH_FILE_LENGTH */
  qa_source_roq_codebooks codebooks;
  uint8_t *book2;  /* views into codebooks */
  uint8_t *book4;
  uint8_t *book8;
  uint8_t *frames; /* owned, QA_ROQ_SCRATCH_FRAMES_LENGTH */
} qa_roq_scratch;

/* False only on OOM. */
bool qa_roq_scratch_init(qa_roq_scratch *scratch);
void qa_roq_scratch_destroy(qa_roq_scratch *scratch);
qa_save_value *qa_roq_scratch_capture_checkpoint(qa_roq_scratch *scratch);
void qa_roq_scratch_restore_checkpoint(qa_roq_scratch *scratch,
                                       const qa_save_value *value);
/* A new CIN_PlayCinematic clears cinematics_t, but the separate
 * global codebooks survive. */
void qa_roq_scratch_clear_movie_state(qa_roq_scratch *scratch);
void qa_roq_scratch_clear(qa_roq_scratch *scratch);
/* Shared physical view: image `index` starts at byteLength*index. */
uint8_t *qa_roq_scratch_frame(qa_roq_scratch *scratch, int64_t byte_length,
                              int index);
/* CIN_UploadCinematic may read beyond the published frame into the
 * same global linbuf. */
uint8_t *qa_roq_scratch_view(qa_roq_scratch *scratch, int64_t offset,
                             int64_t byte_length);

typedef enum qa_roq_event_kind {
  QA_ROQ_EVENT_FRAME,
  QA_ROQ_EVENT_AUDIO,
  QA_ROQ_EVENT_END,
  QA_ROQ_EVENT_INFO,
  QA_ROQ_EVENT_METADATA
} qa_roq_event_kind;

typedef struct qa_roq_event {
  qa_roq_event_kind kind;
  /* frame */
  uint8_t *rgba; /* owned */
  size_t rgba_length;
  int64_t index;
  double time;
  /* audio */
  int16_t *samples; /* owned */
  size_t sample_count;
  int channels;         /* 1 or 2 */
  int64_t sample_rate;  /* 22050 */
  /* info */
  int64_t width;
  int64_t height;
} qa_roq_event;
void qa_roq_event_dispose(qa_roq_event *event);

typedef struct qa_roq_decoder_options {
  bool has_end_policy;
  qa_roq_end_policy end_policy;
  bool silent;
  qa_roq_scratch *scratch; /* borrowed; NULL creates an owned one */
} qa_roq_decoder_options;

typedef enum qa_roq_unknown_chunk {
  QA_ROQ_UNKNOWN_NONE,
  QA_ROQ_UNKNOWN_INTERRUPT,
  QA_ROQ_UNKNOWN_RUN_TAIL
} qa_roq_unknown_chunk;

typedef struct qa_roq_decoder {
  qa_source_roq_audio audio;
  int64_t rate;
  qa_roq_stream *stream; /* borrowed unless owns_stream */
  bool owns_stream;
  uint8_t *book2;
  uint8_t *book4;
  uint8_t *book8;
  qa_roq_scratch *scratch;
  bool owns_scratch;
  qa_roq_scratch owned_scratch;
  int64_t frame_width;
  int64_t frame_height;
  uint8_t *first_buffer;  /* views into scratch frames */
  size_t first_length;
  uint8_t *second_buffer;
  size_t second_length;
  int64_t frame_index;
  qa_roq_unknown_chunk unknown_chunk;
  bool cinematic_lookahead;
  bool silent;
} qa_roq_decoder;

/* `source` NULL means "<RoQ>". False only on OOM. The stream form
 * borrows `stream`; the bytes form borrows `data` via its stream. */
bool qa_roq_decoder_init_stream(qa_roq_decoder *decoder,
                                qa_roq_stream *stream, const char *source,
                                const qa_roq_decoder_options *options);
bool qa_roq_decoder_init_bytes(qa_roq_decoder *decoder, const uint8_t *data,
                               size_t length, const char *source,
                               const qa_roq_decoder_options *options);
void qa_roq_decoder_destroy(qa_roq_decoder *decoder);

qa_save_value *qa_roq_decoder_capture_checkpoint(qa_roq_decoder *decoder);
void qa_roq_decoder_restore_checkpoint(qa_roq_decoder *decoder,
                                       const qa_save_value *value);

int64_t qa_roq_decoder_frame_rate(const qa_roq_decoder *decoder);
int64_t qa_roq_decoder_width(const qa_roq_decoder *decoder);
int64_t qa_roq_decoder_height(const qa_roq_decoder *decoder);
bool qa_roq_decoder_in_packet(const qa_roq_decoder *decoder);
bool qa_roq_decoder_has_invalid_lookahead(const qa_roq_decoder *decoder);
bool qa_roq_decoder_reset_after_run(const qa_roq_decoder *decoder);
/* RoQReset retains both physical image buffers and the codebooks
 * between loops. */
void qa_roq_decoder_rewind(qa_roq_decoder *decoder);
/* False only on OOM; *out always holds the next event on true. */
bool qa_roq_decoder_next(qa_roq_decoder *decoder, qa_roq_event *out);
bool qa_roq_decoder_next_chunk(
    qa_roq_decoder *decoder,
    void (*before_stereo_decode)(void *ctx), void *before_ctx,
    void (*after_info_decode)(void *ctx), void *info_ctx,
    void (*after_audio_decode)(void *ctx, const qa_roq_event *audio),
    void *audio_ctx, qa_roq_event *out);

#endif
