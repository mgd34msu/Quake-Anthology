/*
 * Port of src/audio/wav.ts (from id Software snd_mem.c GetWavinfo /
 * ResampleSfx). Decoded samples are malloc'd and owned by the sound;
 * free with qa_pcm_sound_dispose / qa_decoded_wav_dispose. Decode
 * failures abort via qa_binary_error with the TS BinaryError text.
 * loop_start is -1 for TS null. Functions return false only on OOM.
 * GPL-2.0-or-later.
 */
#ifndef QA_AUDIO_WAV_H
#define QA_AUDIO_WAV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/core/binary/index.h"

typedef struct qa_pcm_sound {
  int64_t sample_rate;
  int channels; /* 1 or 2 */
  int16_t *samples; /* sample_count entries; freed iff samples_owned */
  bool samples_owned;
  size_t sample_count;
  int64_t frame_count;
  int64_t loop_start; /* -1 = null */
} qa_pcm_sound;

typedef struct qa_decoded_wav {
  qa_pcm_sound pcm;
  int source_bytes_per_sample; /* 1, 2 or 3 */
} qa_decoded_wav;

void qa_pcm_sound_dispose(qa_pcm_sound *sound);
void qa_decoded_wav_dispose(qa_decoded_wav *sound);

/* TS WavInfo: holds a reader over the borrowed file allocation. */
typedef struct qa_wav_info {
  qa_binary_reader reader;
  char *source; /* owned copy (reader.source is the same text) */
  int64_t sample_rate;
  int64_t channels;
  int64_t source_bytes_per_sample;
  int64_t frame_count;
  int64_t data_offset;
} qa_wav_info;

typedef void (*qa_wav_print_fn)(const char *text, void *ctx);

/* `bytes` is borrowed mutably, as a Uint8Array reader view in TS.
 * `source` NULL means "<buffer>" behaviours of the reader. */
bool qa_read_wav_info(qa_wav_info *info, uint8_t *bytes, size_t byte_length,
                      int64_t length, const char *source,
                      qa_wav_print_fn print_fn, void *print_ctx);
void qa_wav_info_destroy(qa_wav_info *info);
int64_t qa_wav_info_sample(qa_wav_info *info, int64_t index);
/* TS decode(): mono only. False only on OOM. */
bool qa_wav_info_decode(qa_wav_info *info, qa_pcm_sound *out);

/* `source` NULL means "<buffer>". False only on OOM. */
bool qa_decode_wav(const uint8_t *bytes, size_t length, const char *source,
                   qa_decoded_wav *out);
bool qa_decode_q3_wav(const uint8_t *bytes, size_t length, const char *source,
                      qa_decoded_wav *out);
bool qa_decode_quake_wav(const uint8_t *bytes, size_t length,
                         const char *source, qa_decoded_wav *out);

/*
 * Quiet counterparts for TS callers that catch decode failures
 * (only src/audio/bank.ts: register/openMusic catch -> null). The
 * aborting entry points above remain for callers that do not catch.
 * QA_WAV_TRY_INVALID is exactly the set of inputs on which the
 * aborting decoder would have raised its BinaryError.
 */
typedef enum qa_wav_try_result {
  QA_WAV_TRY_OK,
  QA_WAV_TRY_INVALID,
  QA_WAV_TRY_OOM
} qa_wav_try_result;

qa_wav_try_result qa_try_decode_wav(const uint8_t *bytes, size_t length,
                                    const char *source, qa_decoded_wav *out);
qa_wav_try_result qa_try_decode_q3_wav(const uint8_t *bytes, size_t length,
                                       const char *source,
                                       qa_decoded_wav *out);
qa_wav_try_result qa_try_decode_quake_wav(const uint8_t *bytes, size_t length,
                                          const char *source,
                                          qa_decoded_wav *out);

#endif
