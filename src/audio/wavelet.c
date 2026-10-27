/* Port of src/audio/wavelet.ts. GPL-2.0-or-later. */
#include "qa/audio/wavelet.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static const double C0 = 0.4829629131445341;
static const double C1 = 0.8365163037378079;
static const double C2 = 0.2241438680420134;
static const double C3 = -0.1294095225512604;
enum { CHUNK_SAMPLES = 1024, CHUNK_BYTES = 2048 };

static void integer(int64_t value, int64_t minimum, int64_t maximum,
                    const char *name) {
  if (value < minimum || value > maximum)
    qa_range_error("%s outside %lld..%lld", name, (long long)minimum,
                   (long long)maximum);
}

static float read_f(const float *values, int64_t index, size_t length) {
  if (index < 0 || (uint64_t)index >= length)
    qa_range_error("sound codec read outside allocation at %lld",
                   (long long)index);
  return values[index];
}

static void transform_input(size_t sample_length, int64_t size, int64_t sign) {
  int64_t max_size = sample_length < 4096 ? (int64_t)sample_length : 4096;
  integer(size, 0, max_size, "wavelet size");
  integer(sign, -2147483648LL, 2147483647LL, "wavelet sign");
}

void qa_daub4(float *samples, size_t sample_length, int64_t size, int64_t sign) {
  transform_input(sample_length, size, sign);
  if (size < 4) return;
  if (size % 2 != 0)
    qa_range_error("odd daub4 size reads uninitialized source scratch");
  float *scratch = calloc((size_t)size, sizeof *scratch);
  if (scratch == NULL) abort(); /* no TS counterpart; OOM is fatal here */
  int64_t half = size >> 1;
  if (sign >= 0) {
    int64_t i = 0;
    for (int64_t j = 0; j <= size - 4; j += 2, i++) {
      scratch[i] = (float)(C0 * read_f(samples, j, sample_length) +
                           C1 * read_f(samples, j + 1, sample_length) +
                           C2 * read_f(samples, j + 2, sample_length) +
                           C3 * read_f(samples, j + 3, sample_length));
      scratch[i + half] =
          (float)(C3 * read_f(samples, j, sample_length) -
                  C2 * read_f(samples, j + 1, sample_length) +
                  C1 * read_f(samples, j + 2, sample_length) -
                  C0 * read_f(samples, j + 3, sample_length));
    }
    scratch[i] = (float)(C0 * read_f(samples, size - 2, sample_length) +
                         C1 * read_f(samples, size - 1, sample_length) +
                         C2 * read_f(samples, 0, sample_length) +
                         C3 * read_f(samples, 1, sample_length));
    scratch[i + half] =
        (float)(C3 * read_f(samples, size - 2, sample_length) -
                C2 * read_f(samples, size - 1, sample_length) +
                C1 * read_f(samples, 0, sample_length) -
                C0 * read_f(samples, 1, sample_length));
  } else {
    scratch[0] = (float)(C2 * read_f(samples, half - 1, sample_length) +
                         C1 * read_f(samples, size - 1, sample_length) +
                         C0 * read_f(samples, 0, sample_length) +
                         C3 * read_f(samples, half, sample_length));
    scratch[1] = (float)(C3 * read_f(samples, half - 1, sample_length) -
                         C0 * read_f(samples, size - 1, sample_length) +
                         C1 * read_f(samples, 0, sample_length) -
                         C2 * read_f(samples, half, sample_length));
    for (int64_t i = 0, j = 2; i < half - 1; i++) {
      scratch[j++] = (float)(C2 * read_f(samples, i, sample_length) +
                             C1 * read_f(samples, i + half, sample_length) +
                             C0 * read_f(samples, i + 1, sample_length) +
                             C3 * read_f(samples, i + half + 1, sample_length));
      scratch[j++] = (float)(C3 * read_f(samples, i, sample_length) -
                             C0 * read_f(samples, i + half, sample_length) +
                             C1 * read_f(samples, i + 1, sample_length) -
                             C2 * read_f(samples, i + half + 1, sample_length));
    }
  }
  memcpy(samples, scratch, (size_t)size * sizeof *scratch);
  free(scratch);
}

void qa_wt1(float *samples, size_t sample_length, int64_t size, int64_t sign) {
  transform_input(sample_length, size, sign);
  int64_t inverse_start = size / 4;
  if (inverse_start == 0)
    qa_range_error("wavelet size below four does not terminate in source");
  if (sign >= 0) {
    for (int64_t length = size; length >= inverse_start; length >>= 1)
      qa_daub4(samples, sample_length, length, sign);
  } else {
    for (int64_t length = inverse_start; length <= size; length <<= 1)
      qa_daub4(samples, sample_length, length, sign);
  }
}

int64_t qa_mu_law_encode(int64_t sample) {
  integer(sample, -32768, 32767, "mu-law sample");
  int sign = sample < 0 ? 0 : 0x80;
  int64_t magnitude = sample < 0 ? -sample : sample;
  int64_t adjusted = magnitude + 132 < 32767 ? magnitude + 132 : 32767;
  int shifted = (int)((adjusted >> 7) & 0xff);
  int exponent = 31 - __builtin_clz((unsigned)shifted);
  int64_t mantissa = (adjusted >> (exponent + 3)) & 0xf;
  return (~(sign | (exponent << 4) | (int)mantissa)) & 0xff;
}

int64_t qa_mu_law_decode(int64_t value) {
  integer(value, 0, 255, "mu-law byte");
  int law = (int)(~value) & 0xff;
  int exponent = (law >> 4) & 0x7;
  int mantissa = (law & 0xf) + 16;
  int adjusted = (mantissa << (exponent + 3)) - 132;
  return (law & 0x80) != 0 ? adjusted : -adjusted;
}

static uint8_t *chunk_bytes(const qa_wavelet_sound_chunk *chunk) {
  if (chunk->snd_chunk_length != CHUNK_SAMPLES)
    qa_range_error("sound chunk must contain 1024 shorts");
  return (uint8_t *)(uintptr_t)chunk->snd_chunk;
}

static void output_sample(int16_t *destination, size_t destination_length,
                          int64_t index, double sample) {
  if (index < 0 || (uint64_t)index >= destination_length)
    qa_range_error("sound codec destination is truncated");
  double truncated = trunc(sample);
  if (truncated < -32768 || truncated > 32767)
    qa_range_error("decoded wavelet short outside -32768..32767");
  destination[index] = (int16_t)truncated;
}

void qa_source_wavelet_codec_init(qa_source_wavelet_codec *codec,
                                  qa_wavelet_allocate_fn allocate, void *ctx) {
  memset(codec, 0, sizeof *codec);
  codec->allocate_chunk = allocate;
  codec->allocate_ctx = ctx;
}

void qa_source_wavelet_codec_nx_putc(qa_source_wavelet_codec *codec,
                                     uint8_t *stream, size_t stream_length,
                                     int64_t output) {
  integer(output, -128, 255, "stream character");
  int64_t offset = codec->stream_count++;
  if (offset < 0 || (uint64_t)offset >= stream_length)
    qa_range_error("NXPutc writes outside stream allocation");
  stream[offset] = (uint8_t)output;
}

static void make_table(qa_source_wavelet_codec *codec) {
  if (codec->made_table) return;
  for (int i = 0; i < 256; i++)
    codec->mulaw_to_short[i] = (int16_t)qa_mu_law_decode(i);
  codec->made_table = true;
}

int64_t qa_source_wavelet_codec_mu_law_sample(
    const qa_source_wavelet_codec *codec, int64_t value) {
  integer(value, 0, 255, "mu-law byte");
  return codec->mulaw_to_short[value];
}

static qa_wavelet_sound_chunk *append_chunk(qa_source_wavelet_codec *codec,
                                            qa_wavelet_sound *sound,
                                            qa_wavelet_sound_chunk *previous) {
  qa_wavelet_sound_chunk *chunk = codec->allocate_chunk(codec->allocate_ctx);
  if (sound->sound_data == NULL)
    sound->sound_data = chunk;
  else {
    if (previous == NULL) {
      fprintf(stderr, "Error: source soundData must be null before compression\n");
      abort();
    }
    previous->next = chunk;
  }
  return chunk;
}

static int16_t read_packet(const int16_t *packets, size_t count, int64_t index) {
  if (index < 0 || (uint64_t)index >= count)
    qa_range_error("sound codec read outside allocation at %lld",
                   (long long)index);
  return packets[index];
}

void qa_source_wavelet_codec_encode_wavelet(qa_source_wavelet_codec *codec,
                                            qa_wavelet_sound *sound,
                                            const int16_t *packets,
                                            size_t packet_count) {
  make_table(codec);
  integer(sound->sound_length, 0, 2147483647, "sound length");
  qa_wavelet_sound_chunk *previous = NULL;
  int64_t offset = 0;
  int64_t remaining = sound->sound_length;
  while (remaining > 0) {
    int64_t size = remaining < CHUNK_BYTES ? remaining : CHUNK_BYTES;
    if (size < 4) size = 4;
    qa_wavelet_sound_chunk *chunk = append_chunk(codec, sound, previous);
    previous = chunk;
    float *scratch = calloc((size_t)size, sizeof *scratch);
    if (scratch == NULL) abort();
    for (int64_t i = 0; i < size; i++)
      scratch[i] = (float)read_packet(packets, packet_count, offset++);
    qa_wt1(scratch, (size_t)size, size, 1);
    uint8_t *output = chunk_bytes(chunk);
    for (int64_t i = 0; i < size; i++) {
      double sample = scratch[i];
      if (sample < -32768) sample = -32768;
      if (sample > 32767) sample = 32767;
      output[i] = (uint8_t)qa_mu_law_encode((int64_t)trunc(sample));
    }
    free(scratch);
    chunk->size = size;
    remaining -= size;
  }
}

void qa_source_wavelet_codec_decode_wavelet(qa_source_wavelet_codec *codec,
                                            const qa_wavelet_sound_chunk *chunk,
                                            int16_t *destination,
                                            size_t destination_length) {
  int64_t size = chunk->size;
  integer(size, 0, CHUNK_BYTES, "wavelet chunk size");
  const uint8_t *input = chunk_bytes(chunk);
  float *scratch = calloc(size > 0 ? (size_t)size : 1, sizeof *scratch);
  if (scratch == NULL) abort();
  for (int64_t i = 0; i < size; i++)
    scratch[i] =
        (float)qa_source_wavelet_codec_mu_law_sample(codec, input[i]);
  qa_wt1(scratch, (size_t)size, size, -1);
  if (destination != NULL)
    for (int64_t i = 0; i < size; i++)
      output_sample(destination, destination_length, i, scratch[i]);
  free(scratch);
}

void qa_source_wavelet_codec_encode_mu_law(qa_source_wavelet_codec *codec,
                                           qa_wavelet_sound *sound,
                                           const int16_t *packets,
                                           size_t packet_count) {
  make_table(codec);
  integer(sound->sound_length, 0, 2147483647, "sound length");
  qa_wavelet_sound_chunk *previous = NULL;
  int64_t offset = 0;
  int64_t remaining = sound->sound_length;
  int64_t grade = 0;
  while (remaining > 0) {
    int64_t size = remaining < CHUNK_BYTES ? remaining : CHUNK_BYTES;
    qa_wavelet_sound_chunk *chunk = append_chunk(codec, sound, previous);
    previous = chunk;
    uint8_t *output = chunk_bytes(chunk);
    for (int64_t i = 0; i < size; i++) {
      int64_t sample = read_packet(packets, packet_count, offset) + grade;
      if (sample < -32768) sample = -32768;
      if (sample > 32767) sample = 32767;
      int64_t encoded = qa_mu_law_encode(sample);
      output[i] = (uint8_t)encoded;
      grade = sample - qa_source_wavelet_codec_mu_law_sample(codec, encoded);
      offset++;
    }
    chunk->size = size;
    remaining -= size;
  }
}

void qa_source_wavelet_codec_decode_mu_law(qa_source_wavelet_codec *codec,
                                           const qa_wavelet_sound_chunk *chunk,
                                           int16_t *destination,
                                           size_t destination_length) {
  int64_t size = chunk->size;
  integer(size, 0, CHUNK_BYTES, "mu-law chunk size");
  const uint8_t *input = chunk_bytes(chunk);
  for (int64_t i = 0; i < size; i++)
    output_sample(destination, destination_length, i,
                  (double)qa_source_wavelet_codec_mu_law_sample(codec, input[i]));
}
