/* Port of src/audio/adpcm.ts. GPL-2.0-or-later. */
#include "qa/audio/adpcm.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "qa/foundation/support.h"

static const int index_table[16] = {-1, -1, -1, -1, 2, 4, 6, 8,
                                    -1, -1, -1, -1, 2, 4, 6, 8};
static const int step_size_table[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41,
    45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209,
    230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876,
    963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749,
    3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630,
    9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
    27086, 29794, 32767};

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void require_state(const qa_adpcm_state *state) {
  if (state->sample < -32768 || state->sample > 32767)
    qa_range_error("ADPCM predictor must be a signed 16-bit sample");
  if (state->index < 0 || state->index > 88)
    qa_range_error("ADPCM step index must be between 0 and 88");
}

static int step_at(int64_t index) {
  if (index < 0 || index > 88) qa_range_error("ADPCM table index out of range");
  return step_size_table[index];
}

void qa_adpcm_chunk_init(qa_adpcm_chunk *chunk, uint8_t *data,
                         size_t data_length) {
  if (data_length != QA_ADPCM_CHUNK_BYTES)
    qa_range_error("ADPCM chunk storage must contain 2048 bytes");
  chunk->adpcm.sample = 0;
  chunk->adpcm.index = 0;
  chunk->next = NULL;
  chunk->data = data;
}

void qa_encode_adpcm(const int16_t *samples, size_t sample_count,
                     uint8_t *output, size_t output_length,
                     qa_adpcm_state *state) {
  require_state(state);
  if (output_length < (sample_count + 1) / 2)
    qa_range_error("ADPCM output is too short");
  int predicted = (int)state->sample;
  int index = (int)state->index;
  int step = step_at(index);
  int output_buffer = 0;
  bool high_nibble = true;
  size_t output_offset = 0;
  for (size_t i = 0; i < sample_count; i++) {
    int difference = samples[i] - predicted;
    int sign = difference < 0 ? 8 : 0;
    if (sign != 0) difference = -difference;
    int delta = 0;
    int predicted_difference = step >> 3;
    if (difference >= step) {
      delta = 4;
      difference -= step;
      predicted_difference += step;
    }
    step >>= 1;
    if (difference >= step) {
      delta |= 2;
      difference -= step;
      predicted_difference += step;
    }
    step >>= 1;
    if (difference >= step) {
      delta |= 1;
      predicted_difference += step;
    }
    predicted += sign != 0 ? -predicted_difference : predicted_difference;
    predicted = clampi(predicted, -32768, 32767);
    delta |= sign;
    index = clampi(index + index_table[delta], 0, 88);
    step = step_at(index);
    if (high_nibble)
      output_buffer = (delta << 4) & 0xf0;
    else
      output[output_offset++] = (uint8_t)((delta & 0x0f) | output_buffer);
    high_nibble = !high_nibble;
  }
  if (!high_nibble) output[output_offset] = (uint8_t)output_buffer;
  state->sample = predicted;
  state->index = index;
}

void qa_decode_adpcm(const uint8_t *input, size_t input_length,
                     int16_t *output, size_t output_count,
                     qa_adpcm_state *state) {
  require_state(state);
  if (input_length < (output_count + 1) / 2)
    qa_range_error("ADPCM input is truncated");
  int predicted = (int)state->sample;
  int index = (int)state->index;
  int step = step_at(index);
  int input_buffer = 0;
  for (size_t offset = 0; offset < output_count; offset++) {
    bool high_nibble = (offset & 1) == 0;
    if (high_nibble) input_buffer = input[offset >> 1];
    int code = high_nibble ? (input_buffer >> 4) & 0xf : input_buffer & 0xf;
    index = clampi(index + index_table[code], 0, 88);
    int sign = code & 8;
    int delta = code & 7;
    int predicted_difference = step >> 3;
    if ((delta & 4) != 0) predicted_difference += step;
    if ((delta & 2) != 0) predicted_difference += step >> 1;
    if ((delta & 1) != 0) predicted_difference += step >> 2;
    predicted += sign != 0 ? -predicted_difference : predicted_difference;
    predicted = clampi(predicted, -32768, 32767);
    step = step_at(index);
    output[offset] = (int16_t)predicted;
  }
  state->sample = predicted;
  state->index = index;
}

void qa_decode_adpcm_chunk(const qa_adpcm_chunk *chunk, int16_t *output,
                           size_t output_length) {
  if (output_length < QA_ADPCM_CHUNK_SAMPLES)
    qa_range_error("ADPCM chunk output needs 4096 samples");
  qa_adpcm_state state = chunk->adpcm;
  qa_decode_adpcm(chunk->data, QA_ADPCM_CHUNK_BYTES, output,
                  QA_ADPCM_CHUNK_SAMPLES, &state);
}

void qa_encode_adpcm_sound(const int16_t *samples, size_t sample_count,
                           qa_adpcm_sound *sound,
                           qa_adpcm_allocate_fn allocate, void *ctx) {
  if (sample_count == 0)
    qa_range_error("S_AdpcmEncodeSound requires its initial sample");
  if (sound->sound_data != NULL) {
    fprintf(stderr, "Error: ADPCM soundData must be cleared before encoding\n");
    abort();
  }
  qa_adpcm_state state = {samples[0], 0};
  qa_adpcm_chunk *previous = NULL;
  for (size_t offset = 0; offset < sample_count; offset += QA_ADPCM_CHUNK_SAMPLES) {
    qa_adpcm_chunk *chunk = allocate(ctx);
    chunk->next = NULL;
    if (previous == NULL)
      sound->sound_data = chunk;
    else
      previous->next = chunk;
    previous = chunk;
    chunk->adpcm.sample = state.sample;
    chunk->adpcm.index = state.index;
    size_t count = sample_count - offset;
    if (count > QA_ADPCM_CHUNK_SAMPLES) count = QA_ADPCM_CHUNK_SAMPLES;
    qa_encode_adpcm(samples + offset, count, chunk->data, QA_ADPCM_CHUNK_BYTES,
                    &state);
  }
}

int64_t qa_adpcm_memory_needed(int64_t sample_count, int64_t input_rate,
                               int64_t output_rate) {
  const int64_t values[3] = {sample_count, input_rate, output_rate};
  for (int i = 0; i < 3; i++)
    if (values[i] < 0 || values[i] > 0x7fffffff)
      qa_range_error(
          "ADPCM memory inputs must be nonnegative signed 32-bit integers");
  if (input_rate == 0 || output_rate == 0)
    qa_range_error("ADPCM sample rates must be positive");
  /* Math.fround staging via double->float casts. */
  float scale = (float)((float)input_rate / (float)output_rate);
  double scaled = trunc((double)(float)((float)sample_count / scale));
  if (scaled > 0x7fffffff)
    qa_range_error("ADPCM scaled sample count exceeds signed 32-bit range");
  int64_t scaled_count = (int64_t)scaled;
  int64_t sample_memory = scaled_count / 2;
  int64_t block_count =
      (int64_t)ceil((double)scaled_count / QA_ADPCM_CHUNK_SAMPLES);
  return sample_memory + block_count * 4;
}
