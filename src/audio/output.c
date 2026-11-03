/* Port of src/audio/output.ts. GPL-2.0-or-later. */
#include "qa/audio/output.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/audio/streams.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"

const qa_audio_output_format qa_default_audio_output_format = {44100, 2, 16};
const int64_t qa_audio_output_rates[4] = {11025, 22050, 44100, 48000};

qa_audio_output_format qa_audio_output_format_validated(int64_t sample_rate,
                                                        int64_t channels,
                                                        int64_t sample_bits) {
  if (sample_rate < 8000 || sample_rate > 192000 ||
      (channels != 1 && channels != 2) ||
      (sample_bits != 8 && sample_bits != 16))
    qa_range_error(
        "Audio output requires 8000\xe2\x80\x93" "192000 Hz, 1 or 2 "
        "channels, and 8 or 16 bits");
  qa_audio_output_format format = {sample_rate, channels, sample_bits};
  return format;
}

bool qa_audio_khz_rate(const char *value, int64_t *out) {
  const char *trimmed = value;
  size_t space;
  while ((space = qa_js_white_space_length(trimmed)) != 0) trimmed += space;
  if (*trimmed == '\0') return false; /* value.trim() === "" */
  double number = qa_js_string_to_number(value); /* Number(value): strtod missed "0b1011" */
  if (number == 11) { *out = 11025; return true; }
  if (number == 22) { *out = 22050; return true; }
  if (number == 44) { *out = 44100; return true; }
  if (number == 48) { *out = 48000; return true; }
  return false;
}

bool qa_encode_output_pcm(const int16_t *stereo, size_t sample_count,
                          qa_audio_output_format format,
                          qa_encoded_output_pcm *out) {
  if (sample_count % 2 != 0)
    qa_range_error("Output PCM must contain stereo frames");
  memset(out, 0, sizeof *out);
  if (format.channels == 2 && format.sample_bits == 16) {
    out->samples_s16 = (int16_t *)(uintptr_t)stereo;
    out->borrowed = true;
    out->sample_count = sample_count;
    return true;
  }
  size_t frames = sample_count / 2;
  size_t count = frames * (size_t)format.channels;
  out->is_u8 = format.sample_bits != 16;
  out->sample_count = count;
  if (count > 0) {
    if (out->is_u8) {
      out->samples_u8 = malloc(count);
      if (out->samples_u8 == NULL) return false;
    } else {
      out->samples_s16 = malloc(count * sizeof *out->samples_s16);
      if (out->samples_s16 == NULL) return false;
    }
  }
  for (size_t frame = 0; frame < frames; frame++) {
    int left = stereo[frame * 2], right = stereo[frame * 2 + 1];
    for (int64_t channel = 0; channel < format.channels; channel++) {
      int value = format.channels == 1 ? (left + right) / 2
                                       : channel == 0 ? left : right;
      size_t index = frame * (size_t)format.channels + (size_t)channel;
      if (out->is_u8)
        out->samples_u8[index] = (uint8_t)(((uint32_t)(value + 32768)) >> 8);
      else
        out->samples_s16[index] = (int16_t)value;
    }
  }
  return true;
}

void qa_encoded_output_pcm_dispose(qa_encoded_output_pcm *pcm) {
  if (!pcm->borrowed) {
    free(pcm->samples_s16);
    free(pcm->samples_u8);
  }
  memset(pcm, 0, sizeof *pcm);
}

bool qa_resample_queued_pcm(const int16_t *samples, size_t sample_count,
                            int64_t previous_rate, int64_t next_rate,
                            int16_t **out, size_t *out_count) {
  *out = NULL;
  *out_count = 0;
  if (previous_rate == next_rate || sample_count == 0) {
    if (sample_count > 0) {
      *out = malloc(sample_count * sizeof **out);
      if (*out == NULL) return false;
      memcpy(*out, samples, sample_count * sizeof **out);
      *out_count = sample_count;
    }
    return true;
  }
  qa_raw_audio_stream stream;
  qa_raw_audio_stream_init(&stream, next_rate);
  qa_stream_pcm chunk = {0};
  chunk.samples_s16 = samples;
  chunk.sample_count = sample_count;
  chunk.channels = 2;
  chunk.sample_rate = previous_rate;
  chunk.source_sample = 0;
  chunk.reset_stream = true;
  if (!qa_raw_audio_stream_queue(&stream, &chunk)) {
    qa_raw_audio_stream_destroy(&stream);
    return false;
  }
  int64_t frames =
      (int64_t)ceil((double)sample_count / 2 * (double)next_rate /
                    (double)previous_rate);
  double *mixed = NULL;
  if (frames > 0) {
    mixed = malloc((size_t)frames * 2 * sizeof *mixed);
    if (mixed == NULL) {
      qa_raw_audio_stream_destroy(&stream);
      return false;
    }
  }
  qa_raw_audio_stream_mix(&stream, frames, 1, NULL, NULL, mixed);
  qa_raw_audio_stream_destroy(&stream);
  if (frames > 0) {
    *out = malloc((size_t)frames * 2 * sizeof **out);
    if (*out == NULL) {
      free(mixed);
      return false;
    }
    /* Int16Array.from(Float64Array): truncating conversion. */
    for (int64_t i = 0; i < frames * 2; i++)
      (*out)[i] = qa_js_to_int16(mixed[i]); /* ToInt16: wraps, NaN to 0 */
    *out_count = (size_t)frames * 2;
  }
  free(mixed);
  return true;
}
