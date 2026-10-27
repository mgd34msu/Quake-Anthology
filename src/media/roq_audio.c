/* Port of src/media/roq-audio.ts. GPL-2.0-or-later. */
#include "qa/media/roq_audio.h"

#include <string.h>

#include "qa/foundation/support.h"

void qa_source_roq_audio_init(qa_source_roq_audio *audio) {
  memset(audio, 0, sizeof *audio);
}

void qa_source_roq_audio_setup_table(qa_source_roq_audio *audio) {
  for (int index = 0; index < 128; index++) {
    audio->square[index] = (int16_t)(index * index);
    audio->square[index + 128] = (int16_t)(-index * index);
  }
}

static int byte_at(const uint8_t *input, size_t input_length, int64_t index) {
  if (index < 0 || index >= (int64_t)input_length) {
    qa_range_error("RoQ audio input is truncated");
  }
  return input[index];
}

static void write_at(int16_t *output, size_t output_length, int64_t index,
                     int value) {
  if (index < 0 || index >= (int64_t)output_length) {
    qa_range_error("RoQ audio output is truncated");
  }
  output[index] = (int16_t)value;
}

static void parameters(int64_t size, int64_t flag) {
  if (size < 0 || size > 0xffffffffLL) {
    qa_range_error("RoQ audio size requires an unsigned int");
  }
  if (flag < 0 || flag > 65535) {
    qa_range_error("RoQ audio flag requires an unsigned short");
  }
}

static int delta(const qa_source_roq_audio *audio, int value) {
  if (value < 0 || value > 255) {
    qa_range_error("RoQ square table index is invalid");
  }
  return audio->square[value];
}

static int wrap16(int value) {
  return (int32_t)(((uint32_t)value << 16)) >> 16;
}

int64_t qa_source_roq_audio_decode_mono_to_mono(
    qa_source_roq_audio *audio, const uint8_t *input, size_t input_length,
    int16_t *output, size_t output_length, int64_t size, bool signed_output,
    int64_t flag) {
  parameters(size, flag);
  int previous = signed_output ? (int)flag - 0x8000 : (int)flag;
  for (int64_t index = 0; index < size; index++) {
    previous =
        wrap16(previous + delta(audio, byte_at(input, input_length, index)));
    write_at(output, output_length, index, previous);
  }
  return size;
}

int64_t qa_source_roq_audio_decode_mono_to_stereo(
    qa_source_roq_audio *audio, const uint8_t *input, size_t input_length,
    int16_t *output, size_t output_length, int64_t size, bool signed_output,
    int64_t flag) {
  parameters(size, flag);
  int previous = signed_output ? (int)flag - 0x8000 : (int)flag;
  for (int64_t index = 0; index < size; index++) {
    previous =
        wrap16(previous + delta(audio, byte_at(input, input_length, index)));
    /* The C chained assignment writes the right-hand sample first. */
    write_at(output, output_length, index * 2 + 1, previous);
    write_at(output, output_length, index * 2, previous);
  }
  return size;
}

int64_t qa_source_roq_audio_decode_stereo_to_stereo(
    qa_source_roq_audio *audio, const uint8_t *input, size_t input_length,
    int16_t *output, size_t output_length, int64_t size, bool signed_output,
    int64_t flag) {
  parameters(size, flag);
  int left = (int)(flag & 0xff00) - (signed_output ? 0x8000 : 0);
  int right = (int)((flag & 0xff) << 8) - (signed_output ? 0x8000 : 0);
  for (int64_t index = 0; index < size; index += 2) {
    left = wrap16(left + delta(audio, byte_at(input, input_length, index)));
    right = wrap16(right +
                   delta(audio, byte_at(input, input_length, index + 1)));
    write_at(output, output_length, index, left);
    write_at(output, output_length, index + 1, right);
  }
  return (int64_t)((uint32_t)size >> 1);
}

int64_t qa_source_roq_audio_decode_stereo_to_mono(
    qa_source_roq_audio *audio, const uint8_t *input, size_t input_length,
    int16_t *output, size_t output_length, int64_t size, bool signed_output,
    int64_t flag) {
  parameters(size, flag);
  int32_t left = (int32_t)(flag & 0xff00) - (signed_output ? 0x8000 : 0);
  int32_t right = (int32_t)((flag & 0xff) << 8) - (signed_output ? 0x8000 : 0);
  for (int64_t index = 0; index < size; index++) {
    left = (int32_t)((uint32_t)left +
                     (uint32_t)delta(audio, byte_at(input, input_length,
                                                     index * 2)));
    right = (int32_t)((uint32_t)right +
                      (uint32_t)delta(audio, byte_at(input, input_length,
                                                      index * 2 + 1)));
    int32_t sum = (int32_t)((uint32_t)left + (uint32_t)right);
    write_at(output, output_length, index, sum / 2);
  }
  return size;
}
