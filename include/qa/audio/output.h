/*
 * Port of src/audio/output.ts. audioOutputFormat's `unknown` input
 * becomes the three field values (presence checks have no C form).
 * encodeOutputPcm's union return: samples_s16 borrowed from the
 * input when stereo/16-bit (TS returns the same array), otherwise
 * exactly one of samples_s16/samples_u8 is malloc'd; free with
 * qa_encoded_output_pcm_dispose. The format "sampleBits" text uses
 * the exact TS en dash in the RangeError message.
 * GPL-2.0-or-later.
 */
#ifndef QA_AUDIO_OUTPUT_H
#define QA_AUDIO_OUTPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* TS AudioOutputFormat = Pick<SdlAudioOptions,...> & {sampleBits}.
 * TS owner: src/platform/audio.ts (SdlAudioOptions has no C home in
 * include/qa/platform/ yet); only the picked fields are mirrored. */
typedef struct qa_audio_output_format {
  int64_t sample_rate;
  int64_t channels;    /* 1 or 2 */
  int64_t sample_bits; /* 8 or 16 */
} qa_audio_output_format;

extern const qa_audio_output_format qa_default_audio_output_format;
extern const int64_t qa_audio_output_rates[4];

/* Aborts with the TS RangeError when any field is out of range. */
qa_audio_output_format qa_audio_output_format_validated(int64_t sample_rate,
                                                        int64_t channels,
                                                        int64_t sample_bits);
/* TS number|null: false means null. JS Number() parsing via strtod
 * (trailing junk yields null, as NaN does in the switch). */
bool qa_audio_khz_rate(const char *value, int64_t *out);

typedef struct qa_encoded_output_pcm {
  bool is_u8;
  bool borrowed; /* samples_s16 aliases the input; do not free */
  int16_t *samples_s16;
  uint8_t *samples_u8;
  size_t sample_count;
} qa_encoded_output_pcm;

bool qa_encode_output_pcm(const int16_t *stereo, size_t sample_count,
                          qa_audio_output_format format,
                          qa_encoded_output_pcm *out);
void qa_encoded_output_pcm_dispose(qa_encoded_output_pcm *pcm);

/* Malloc'd copy/resample; caller frees *out. False only on OOM. */
bool qa_resample_queued_pcm(const int16_t *samples, size_t sample_count,
                            int64_t previous_rate, int64_t next_rate,
                            int16_t **out, size_t *out_count);

#endif
