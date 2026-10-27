/*
 * Port of src/audio/wavelet.ts (Q3 snd_wavelet.c). Chunk storage is
 * allocator-owned (1024 shorts); the allocator closure becomes a
 * function pointer plus ctx. GPL-2.0-or-later.
 */
#ifndef QA_AUDIO_WAVELET_H
#define QA_AUDIO_WAVELET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct qa_wavelet_sound_chunk {
  int16_t *snd_chunk; /* borrowed; 1024 shorts (snd_chunk_length records it) */
  size_t snd_chunk_length;
  struct qa_wavelet_sound_chunk *next;
  int64_t size;
} qa_wavelet_sound_chunk;

typedef struct qa_wavelet_sound {
  int64_t sound_length;
  qa_wavelet_sound_chunk *sound_data;
} qa_wavelet_sound;

/* Float32Array staged in place: samples are binary32, expressions
 * computed in double and stored to float as in TS. */
void qa_daub4(float *samples, size_t sample_length, int64_t size, int64_t sign);
void qa_wt1(float *samples, size_t sample_length, int64_t size, int64_t sign);
int64_t qa_mu_law_encode(int64_t sample);
int64_t qa_mu_law_decode(int64_t value);

typedef qa_wavelet_sound_chunk *(*qa_wavelet_allocate_fn)(void *ctx);

typedef struct qa_source_wavelet_codec {
  int16_t mulaw_to_short[256];
  bool made_table;
  int64_t stream_count;
  qa_wavelet_allocate_fn allocate_chunk;
  void *allocate_ctx;
} qa_source_wavelet_codec;

void qa_source_wavelet_codec_init(qa_source_wavelet_codec *codec,
                                  qa_wavelet_allocate_fn allocate, void *ctx);
void qa_source_wavelet_codec_nx_putc(qa_source_wavelet_codec *codec,
                                     uint8_t *stream, size_t stream_length,
                                     int64_t output);
int64_t qa_source_wavelet_codec_mu_law_sample(
    const qa_source_wavelet_codec *codec, int64_t value);
void qa_source_wavelet_codec_encode_wavelet(qa_source_wavelet_codec *codec,
                                            qa_wavelet_sound *sound,
                                            const int16_t *packets,
                                            size_t packet_count);
/* destination NULL mirrors the TS null destination. */
void qa_source_wavelet_codec_decode_wavelet(qa_source_wavelet_codec *codec,
                                            const qa_wavelet_sound_chunk *chunk,
                                            int16_t *destination,
                                            size_t destination_length);
void qa_source_wavelet_codec_encode_mu_law(qa_source_wavelet_codec *codec,
                                           qa_wavelet_sound *sound,
                                           const int16_t *packets,
                                           size_t packet_count);
void qa_source_wavelet_codec_decode_mu_law(qa_source_wavelet_codec *codec,
                                           const qa_wavelet_sound_chunk *chunk,
                                           int16_t *destination,
                                           size_t destination_length);

#endif
