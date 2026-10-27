/*
 * Port of src/audio/adpcm.ts (Q3 snd_adpcm.c IMA ADPCM). Chunk data
 * storage is caller-owned (2048 bytes), as in TS. The chunk allocator
 * closure becomes a function pointer plus ctx. GPL-2.0-or-later.
 */
#ifndef QA_AUDIO_ADPCM_H
#define QA_AUDIO_ADPCM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define QA_ADPCM_CHUNK_BYTES 2048
#define QA_ADPCM_CHUNK_SAMPLES 4096

typedef struct qa_adpcm_state {
  int64_t sample;
  int64_t index;
} qa_adpcm_state;

typedef struct qa_adpcm_chunk {
  qa_adpcm_state adpcm;
  struct qa_adpcm_chunk *next;
  uint8_t *data; /* borrowed; must hold QA_ADPCM_CHUNK_BYTES */
} qa_adpcm_chunk;

/* TS AdpcmSound { soundData }. */
typedef struct qa_adpcm_sound {
  qa_adpcm_chunk *sound_data;
} qa_adpcm_sound;

typedef qa_adpcm_chunk *(*qa_adpcm_allocate_fn)(void *ctx);

/* Validates the 2048-byte storage length like the TS constructor:
 * callers pass the length they allocated. Aborts on mismatch. */
void qa_adpcm_chunk_init(qa_adpcm_chunk *chunk, uint8_t *data,
                         size_t data_length);

void qa_encode_adpcm(const int16_t *samples, size_t sample_count,
                     uint8_t *output, size_t output_length,
                     qa_adpcm_state *state);
void qa_decode_adpcm(const uint8_t *input, size_t input_length,
                     int16_t *output, size_t output_count,
                     qa_adpcm_state *state);
void qa_decode_adpcm_chunk(const qa_adpcm_chunk *chunk, int16_t *output,
                           size_t output_length);
void qa_encode_adpcm_sound(const int16_t *samples, size_t sample_count,
                           qa_adpcm_sound *sound,
                           qa_adpcm_allocate_fn allocate, void *ctx);
int64_t qa_adpcm_memory_needed(int64_t sample_count, int64_t input_rate,
                               int64_t output_rate);

#endif
