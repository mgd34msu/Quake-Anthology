/*
 * Port of src/media/roq-audio.ts (RllSetupTable/RllDecode from
 * id Software cl_cin.c). The square table is zero until
 * qa_source_roq_audio_setup_table, as before initRoQ. Input/output
 * lengths reproduce the TS truncated-buffer RangeErrors.
 * GPL-2.0-or-later.
 */
#ifndef QA_MEDIA_ROQ_AUDIO_H
#define QA_MEDIA_ROQ_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct qa_source_roq_audio {
  int16_t square[256];
} qa_source_roq_audio;

void qa_source_roq_audio_init(qa_source_roq_audio *audio);
void qa_source_roq_audio_setup_table(qa_source_roq_audio *audio);

int64_t qa_source_roq_audio_decode_mono_to_mono(
    qa_source_roq_audio *audio, const uint8_t *input, size_t input_length,
    int16_t *output, size_t output_length, int64_t size, bool signed_output,
    int64_t flag);
int64_t qa_source_roq_audio_decode_mono_to_stereo(
    qa_source_roq_audio *audio, const uint8_t *input, size_t input_length,
    int16_t *output, size_t output_length, int64_t size, bool signed_output,
    int64_t flag);
int64_t qa_source_roq_audio_decode_stereo_to_stereo(
    qa_source_roq_audio *audio, const uint8_t *input, size_t input_length,
    int16_t *output, size_t output_length, int64_t size, bool signed_output,
    int64_t flag);
/* Source size counts output mono samples; consumes twice as many
 * input bytes. */
int64_t qa_source_roq_audio_decode_stereo_to_mono(
    qa_source_roq_audio *audio, const uint8_t *input, size_t input_length,
    int16_t *output, size_t output_length, int64_t size, bool signed_output,
    int64_t flag);

#endif
