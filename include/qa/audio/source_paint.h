/*
 * Port of src/audio/source-paint.ts (Q3 snd_mix/snd_dma/snd_mem).
 * SourcePaintChunk is both an AdpcmChunk (data bytes) and a
 * WaveletSoundChunk (snd_chunk views the same 2048 bytes as 1024
 * shorts; produced by qa_source_paint_chunk_wavelet_view). DMA
 * buffer samples are one buffer viewed as int16 or uint8 per
 * samplebits. GPL-2.0-or-later.
 */
#ifndef QA_AUDIO_SOURCE_PAINT_H
#define QA_AUDIO_SOURCE_PAINT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/audio/adpcm.h"
#include "qa/audio/wavelet.h"

typedef struct qa_source_paint_chunk {
  qa_adpcm_state adpcm;
  struct qa_source_paint_chunk *next;
  uint8_t *data;    /* borrowed; 2048 bytes */
  int16_t *snd_chunk; /* (int16_t *)data, 1024 shorts */
  int64_t size;
} qa_source_paint_chunk;

/* Validates the 2048-byte storage like the AdpcmChunk constructor
 * (snd_chunk then spans 1024 shorts of the same bytes). */
void qa_source_paint_chunk_init(qa_source_paint_chunk *chunk, uint8_t *data,
                                size_t data_length);
/* Wavelet view of this chunk for codec calls (next not followed). */
qa_wavelet_sound_chunk qa_source_paint_chunk_wavelet_view(
    const qa_source_paint_chunk *chunk);
/* AdpcmChunk view for decodeAdpcmChunk (data/state shared). */
qa_adpcm_chunk qa_source_paint_chunk_adpcm_view(
    const qa_source_paint_chunk *chunk);

typedef struct qa_source_paint_sound {
  qa_source_paint_chunk *sound_data;
} qa_source_paint_sound;

typedef struct qa_source_paint_channel {
  int64_t leftvol;
  int64_t rightvol;
  bool doppler;
  double doppler_scale;     /* binary32 in TS use */
  double old_doppler_scale; /* binary32 in TS use */
} qa_source_paint_channel;

typedef struct qa_source_sound_resampler {
  int64_t count;
  int64_t step;
} qa_source_sound_resampler;

void qa_source_sound_resampler_init(qa_source_sound_resampler *resampler,
                                    double input_rate, double output_rate,
                                    double sample_count);
int64_t qa_source_sound_resampler_source_index(
    const qa_source_sound_resampler *resampler, int64_t frame);

/* data is read little-endian (DataView), as in TS. */
int64_t qa_resample_sound_raw(int16_t *output, size_t output_length,
                              double input_rate, double output_rate,
                              int64_t width, double samples,
                              const uint8_t *data, size_t data_length);

/* Two TS overloads (Int32Array / Float64Array paint). */
void qa_write_linear_blast_stereo16_i32(const int32_t *paint,
                                        size_t paint_length, int16_t *output,
                                        size_t output_length, int64_t count);
void qa_write_linear_blast_stereo16_f64(const double *paint,
                                        size_t paint_length, int16_t *output,
                                        size_t output_length, int64_t count);

typedef struct qa_source_dma_buffer {
  int samplebits; /* 8 or 16 */
  int channels;   /* 1 or 2 */
  void *samples;  /* int16_t* when 16, uint8_t* when 8 */
  size_t sample_capacity;
} qa_source_dma_buffer;

void qa_byte_swap_raw_samples(int64_t samples, int64_t width, int64_t channels,
                              uint8_t *data, size_t data_length,
                              bool little_endian);
void qa_transfer_paint_buffer(int32_t *paint, size_t paint_length,
                              qa_source_dma_buffer *dma, int64_t painted_time,
                              int64_t end_time, bool test_sound);

typedef struct qa_source_compressed_painter {
  qa_source_wavelet_codec *codec; /* borrowed */
  int16_t scratch[4096];
  const qa_source_paint_sound *scratch_sound;
  int64_t scratch_index;
} qa_source_compressed_painter;

void qa_source_compressed_painter_init(
    qa_source_compressed_painter *painter, qa_source_wavelet_codec *codec);
void qa_source_compressed_painter_paint_adpcm(
    qa_source_compressed_painter *painter,
    const qa_source_paint_sound *sound,
    const qa_source_paint_channel *channel, int32_t *paint,
    size_t paint_length, int64_t count, double sample_offset,
    int64_t buffer_offset, int64_t volume);
void qa_source_compressed_painter_paint_wavelet(
    qa_source_compressed_painter *painter,
    const qa_source_paint_sound *sound,
    const qa_source_paint_channel *channel, int32_t *paint,
    size_t paint_length, int64_t count, int64_t sample_offset,
    int64_t buffer_offset, int64_t volume);
void qa_source_compressed_painter_paint_mu_law(
    qa_source_compressed_painter *painter,
    const qa_source_paint_sound *sound,
    const qa_source_paint_channel *channel, int32_t *paint,
    size_t paint_length, int64_t count, int64_t sample_offset,
    int64_t buffer_offset, int64_t volume);

#endif
