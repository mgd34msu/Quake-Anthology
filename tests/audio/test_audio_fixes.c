/*
 * 8-bit WAV samples: TS (u8 - 128) << 8 without a C left shift of a negative
 * value (UBSan). bun: decodeWav of an 8-bit mono file holding
 * [0, 1, 127, 128, 129, 255] -> [-32768, -32512, -256, 0, 256, 32512].
 * GPL-2.0-or-later.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "qa/audio/wav.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                                    \
  do {                                                                                 \
    checks++;                                                                          \
    if (!(cond)) {                                                                     \
      failures++;                                                                      \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                 \
    }                                                                                  \
  } while (0)

static void put32(uint8_t *at, uint32_t value) {
  for (int i = 0; i < 4; i++) at[i] = (uint8_t)(value >> (8 * i));
}
static void put16(uint8_t *at, uint16_t value) {
  at[0] = (uint8_t)value;
  at[1] = (uint8_t)(value >> 8);
}

static void test_eight_bit_samples(void) {
  static const uint8_t pcm[] = {0, 1, 127, 128, 129, 255};
  static const int16_t expected[] = {-32768, -32512, -256, 0, 256, 32512};
  uint8_t file[44 + sizeof pcm];
  memset(file, 0, sizeof file);
  memcpy(file, "RIFF", 4);
  put32(file + 4, (uint32_t)(36 + sizeof pcm));
  memcpy(file + 8, "WAVEfmt ", 8);
  put32(file + 16, 16);
  put16(file + 20, 1);
  put16(file + 22, 1);
  put32(file + 24, 11025);
  put32(file + 28, 11025);
  put16(file + 32, 1);
  put16(file + 34, 8);
  memcpy(file + 36, "data", 4);
  put32(file + 40, sizeof pcm);
  memcpy(file + 44, pcm, sizeof pcm);
  qa_decoded_wav decoded;
  memset(&decoded, 0, sizeof decoded);
  CHECK(qa_decode_wav(file, sizeof file, NULL, &decoded));
  CHECK(decoded.pcm.sample_count == sizeof pcm);
  if (decoded.pcm.sample_count == sizeof pcm)
    for (size_t i = 0; i < sizeof pcm; i++) CHECK(decoded.pcm.samples[i] == expected[i]);
  qa_decoded_wav_dispose(&decoded);
}

int main(void) {
  test_eight_bit_samples();
  if (failures) {
    fprintf(stderr, "%d of %d check(s) failed\n", failures, checks);
    return 1;
  }
  printf("test_audio_fixes: %d checks ok\n", checks);
  return 0;
}
