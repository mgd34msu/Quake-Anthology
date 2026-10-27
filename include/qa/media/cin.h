/*
 * Port of src/media/cin.ts (Quake II cl_cin.c order-1 Huffman CIN
 * decoding). C accommodations: decoded pixels and PCM are owned
 * malloc'd buffers in the frame (dispose frees them); the decoder
 * owns its MediaInput; close closes the input but keeps the struct
 * (next/rewind then abort, as the TS throws), destroy also frees.
 * GPL-2.0-or-later.
 */
#ifndef QA_MEDIA_CIN_H
#define QA_MEDIA_CIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/media/source.h"
#include "qa/persistence/value.h"

typedef struct qa_cin_audio_format {
  int64_t sample_rate;
  int channels;     /* 1 or 2 */
  int sample_bytes; /* 1 or 2 */
} qa_cin_audio_format;

typedef struct qa_cin_audio {
  bool is_s16;
  int16_t *samples_s16; /* owned when is_s16 */
  uint8_t *samples_u8;  /* owned otherwise */
  size_t sample_count;
  int channels; /* 1 or 2 */
  int64_t sample_rate;
  int64_t source_sample;
} qa_cin_audio;
void qa_cin_audio_dispose(qa_cin_audio *audio);

/* kind is "frame"; is_end marks the TS {kind:"end"} result. */
typedef struct qa_cin_frame {
  bool is_end;
  int64_t index;
  double time;
  uint8_t *pixels; /* owned */
  size_t pixels_length;
  uint8_t palette[768];
  bool has_audio;
  qa_cin_audio audio;
} qa_cin_frame;
void qa_cin_frame_dispose(qa_cin_frame *frame);

/* C's integer division partitions rates not divisible by 14. */
void qa_cin_sample_range(int64_t frame, int64_t sample_rate, int64_t *start,
                         int64_t *end);

typedef struct qa_cin_huffman {
  int32_t *nodes; /* owned, 256*256*2 */
  int32_t roots[256];
  char *source; /* owned */
} qa_cin_huffman;

/* `source` NULL means "<CIN Huffman>". False only on OOM. */
bool qa_cin_huffman_init(qa_cin_huffman *huffman, const uint8_t *counts,
                         size_t counts_length, const char *source);
void qa_cin_huffman_destroy(qa_cin_huffman *huffman);
/* Malloc'd decoded pixels; caller frees. NULL only on OOM. */
uint8_t *qa_cin_huffman_decode(qa_cin_huffman *huffman,
                               const uint8_t *compressed, size_t length,
                               int64_t expected_pixels);

typedef struct qa_cin_decoder {
  int64_t width;
  int64_t height;
  int64_t frame_rate; /* 14 */
  bool has_audio_format;
  qa_cin_audio_format audio_format;
  qa_media_input *input; /* owned; NULL once closed */
  int64_t input_length;
  qa_cin_huffman huffman;
  uint8_t colors[768];
  int64_t offset;
  int64_t frame_index;
  bool ended;
  bool closed;
} qa_cin_decoder;

/* Take ownership of `input` (closed on failure, as in the TS).
 * `source` NULL means "<CIN>". False only on OOM. */
bool qa_cin_decoder_init(qa_cin_decoder *decoder, qa_media_input *input,
                         const char *source);
/* Bytes form; borrows `data` through a mediaBytes input. */
bool qa_cin_decoder_init_bytes(qa_cin_decoder *decoder, const uint8_t *data,
                               size_t length, const char *source);
void qa_cin_decoder_destroy(qa_cin_decoder *decoder);

qa_save_value *qa_cin_decoder_capture_checkpoint(qa_cin_decoder *decoder);
void qa_cin_decoder_restore_checkpoint(qa_cin_decoder *decoder,
                                       const qa_save_value *value);
/* Copies the current 768-byte palette into `out`. */
void qa_cin_decoder_palette(const qa_cin_decoder *decoder, uint8_t out[768]);
int64_t qa_cin_decoder_next_frame_index(const qa_cin_decoder *decoder);
/* False only on OOM; *out holds a frame or the end marker. */
bool qa_cin_decoder_next(qa_cin_decoder *decoder, qa_cin_frame *out);
void qa_cin_decoder_rewind(qa_cin_decoder *decoder);
void qa_cin_decoder_close(qa_cin_decoder *decoder);

/* Malloc'd RGBA pixels (length pixels_length*4); caller frees.
 * NULL only on OOM. */
uint8_t *qa_cin_rgba(const uint8_t *pixels, size_t pixels_length,
                     const uint8_t *palette, size_t palette_length);

#endif
