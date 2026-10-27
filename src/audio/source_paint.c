/* Port of src/audio/source-paint.ts. GPL-2.0-or-later. */
#include "qa/audio/source_paint.h"

#include <math.h>
#include <string.h>

#include "qa/foundation/support.h"

void qa_source_paint_chunk_init(qa_source_paint_chunk *chunk, uint8_t *data,
                                size_t data_length) {
  if (data_length != QA_ADPCM_CHUNK_BYTES)
    qa_range_error("ADPCM chunk storage must contain 2048 bytes");
  chunk->adpcm.sample = 0;
  chunk->adpcm.index = 0;
  chunk->next = NULL;
  chunk->data = data;
  chunk->snd_chunk = (int16_t *)(uintptr_t)data;
  chunk->size = 0;
}

qa_wavelet_sound_chunk qa_source_paint_chunk_wavelet_view(
    const qa_source_paint_chunk *chunk) {
  qa_wavelet_sound_chunk view;
  view.snd_chunk = chunk->snd_chunk;
  view.snd_chunk_length = 1024;
  view.next = NULL;
  view.size = chunk->size;
  return view;
}

qa_adpcm_chunk qa_source_paint_chunk_adpcm_view(
    const qa_source_paint_chunk *chunk) {
  qa_adpcm_chunk view;
  view.adpcm = chunk->adpcm;
  view.next = NULL;
  view.data = chunk->data;
  return view;
}

void qa_source_sound_resampler_init(qa_source_sound_resampler *resampler,
                                    double input_rate, double output_rate,
                                    double sample_count) {
  /* Math.fround staging via double->float casts. */
  float scale = (float)((float)input_rate / (float)output_rate);
  double count = trunc((double)(float)((float)sample_count / scale));
  double step = trunc((double)(float)(scale * 256));
  if (count < -2147483648.0 || count > 2147483647.0 || step < -2147483648.0 ||
      step > 2147483647.0)
    qa_range_error("Resampled sound has an undefined signed-int conversion");
  resampler->count = (int64_t)count;
  resampler->step = (int64_t)step;
}

int64_t qa_source_sound_resampler_source_index(
    const qa_source_sound_resampler *resampler, int64_t frame) {
  /* Math.imul(frame, step) >> 8: signed 32-bit wrap. */
  int32_t product = (int32_t)((uint32_t)frame * (uint32_t)resampler->step);
  return product >> 8;
}

static int16_t load_i16_le(const uint8_t *data, size_t length, int64_t offset) {
  if (offset < 0 || (uint64_t)offset + 2 > length)
    qa_range_error("Sound paint access outside allocation at %lld",
                   (long long)offset);
  return (int16_t)((uint16_t)data[offset] | ((uint16_t)data[offset + 1] << 8));
}

static uint8_t load_u8(const uint8_t *data, size_t length, int64_t offset) {
  if (offset < 0 || (uint64_t)offset >= length)
    qa_range_error("Sound paint access outside allocation at %lld",
                   (long long)offset);
  return data[offset];
}

int64_t qa_resample_sound_raw(int16_t *output, size_t output_length,
                              double input_rate, double output_rate,
                              int64_t width, double samples,
                              const uint8_t *data, size_t data_length) {
  qa_source_sound_resampler resampler;
  qa_source_sound_resampler_init(&resampler, input_rate, output_rate, samples);
  for (int64_t frame = 0; frame < resampler.count; frame++) {
    int64_t source = qa_source_sound_resampler_source_index(&resampler, frame);
    int16_t sample = width == 2
                         ? load_i16_le(data, data_length, source * 2)
                         : (int16_t)((load_u8(data, data_length, source) - 128)
                                     << 8);
    if (frame >= (int64_t)output_length)
      qa_range_error("ResampleSfxRaw output is truncated");
    output[frame] = sample;
  }
  return resampler.count;
}

static int16_t clipped_i32(int32_t value) {
  int32_t shifted = value >> 8;
  if (shifted < -32768) return -32768;
  if (shifted > 32767) return 32767;
  return (int16_t)shifted;
}

static int32_t paint_i32(const int32_t *paint, size_t length, int64_t index) {
  if (index < 0 || (uint64_t)index >= length)
    qa_range_error("Sound paint access outside allocation at %lld",
                   (long long)index);
  return paint[index];
}

static double paint_f64(const double *paint, size_t length, int64_t index) {
  if (index < 0 || (uint64_t)index >= length)
    qa_range_error("Sound paint access outside allocation at %lld",
                   (long long)index);
  return paint[index];
}

void qa_write_linear_blast_stereo16_i32(const int32_t *paint,
                                        size_t paint_length, int16_t *output,
                                        size_t output_length, int64_t count) {
  if (count < 0 || count % 2 != 0)
    qa_range_error("Stereo blast requires an even sample count");
  for (int64_t index = 0; index < count; index += 2) {
    if (index + 1 >= (int64_t)output_length)
      qa_range_error("Stereo output allocation is truncated");
    output[index] = clipped_i32(paint_i32(paint, paint_length, index));
    output[index + 1] = clipped_i32(paint_i32(paint, paint_length, index + 1));
  }
}

void qa_write_linear_blast_stereo16_f64(const double *paint,
                                        size_t paint_length, int16_t *output,
                                        size_t output_length, int64_t count) {
  if (count < 0 || count % 2 != 0)
    qa_range_error("Stereo blast requires an even sample count");
  for (int64_t index = 0; index < count; index += 2) {
    if (index + 1 >= (int64_t)output_length)
      qa_range_error("Stereo output allocation is truncated");
    double left = floor(paint_f64(paint, paint_length, index) / 256);
    double right = floor(paint_f64(paint, paint_length, index + 1) / 256);
    output[index] =
        (int16_t)(left < -32768 ? -32768 : left > 32767 ? 32767 : left);
    output[index + 1] =
        (int16_t)(right < -32768 ? -32768 : right > 32767 ? 32767 : right);
  }
}

void qa_byte_swap_raw_samples(int64_t samples, int64_t width, int64_t channels,
                              uint8_t *data, size_t data_length,
                              bool little_endian) {
  if (width != 2 || little_endian) return;
  if (channels == 2) samples <<= 1;
  for (int64_t index = 0; index < samples; index++) {
    int64_t offset = index * 2;
    uint8_t first = load_u8(data, data_length, offset);
    uint8_t second = load_u8(data, data_length, offset + 1);
    data[offset] = second;
    data[offset + 1] = first;
  }
}

void qa_transfer_paint_buffer(int32_t *paint, size_t paint_length,
                              qa_source_dma_buffer *dma, int64_t painted_time,
                              int64_t end_time, bool test_sound) {
  int64_t frames = end_time - painted_time;
  size_t capacity = dma->sample_capacity;
  if (frames < 0 || capacity < (size_t)dma->channels ||
      (capacity & (capacity - 1)) != 0)
    qa_range_error("Invalid source DMA paint range or ring capacity");
  if (test_sound) {
    for (int64_t index = 0; index < frames; index++) {
      if (index * 2 + 1 >= (int64_t)paint_length)
        qa_range_error("Paint allocation is truncated");
      paint[index * 2] = paint[index * 2 + 1] =
          (int32_t)trunc(sin((painted_time + (double)index) * 0.1) * 20000 * 256);
    }
  }
  int64_t count = frames * dma->channels;
  int64_t step = 3 - dma->channels;
  size_t destination =
      (size_t)((painted_time * dma->channels) & (int64_t)(capacity - 1));
  for (int64_t index = 0; index < count; index++) {
    int16_t sample = clipped_i32(paint_i32(paint, paint_length, index * step));
    if (dma->samplebits == 16)
      ((int16_t *)dma->samples)[destination] = sample;
    else
      ((uint8_t *)dma->samples)[destination] = (uint8_t)((sample >> 8) + 128);
    destination = (destination + 1) & (capacity - 1);
  }
}

void qa_source_compressed_painter_init(
    qa_source_compressed_painter *painter, qa_source_wavelet_codec *codec) {
  memset(painter, 0, sizeof *painter);
  painter->codec = codec;
}

static qa_source_paint_chunk *chunk_or_fail(qa_source_paint_chunk *chunk) {
  if (chunk == NULL)
    qa_range_error("Sound paint reached a null source chunk");
  return chunk;
}

static int32_t imul32(int64_t a, int64_t b) {
  return (int32_t)((uint32_t)a * (uint32_t)b);
}

static void painter_add(qa_source_compressed_painter *painter, int32_t *paint,
                        size_t paint_length, int64_t index, int64_t sample,
                        const qa_source_paint_channel *channel,
                        int64_t volume) {
  (void)painter;
  int32_t left = imul32(sample, imul32(channel->leftvol, volume)) >> 8;
  int32_t right = imul32(sample, imul32(channel->rightvol, volume)) >> 8;
  int32_t existing_left = paint_i32(paint, paint_length, index * 2);
  int32_t existing_right = paint_i32(paint, paint_length, index * 2 + 1);
  paint[index * 2] = (int32_t)((uint32_t)existing_left + (uint32_t)left);
  paint[index * 2 + 1] = (int32_t)((uint32_t)existing_right + (uint32_t)right);
}

static int16_t scratch_at(const int16_t *scratch, int64_t index) {
  if (index < 0 || index >= 4096)
    qa_range_error("Sound paint access outside allocation at %lld",
                   (long long)index);
  return scratch[index];
}

static void decode_current_adpcm(const qa_source_paint_chunk *chunk,
                                 int16_t *scratch) {
  qa_adpcm_chunk view = qa_source_paint_chunk_adpcm_view(chunk);
  qa_decode_adpcm_chunk(&view, scratch, 4096);
}

void qa_source_compressed_painter_paint_adpcm(
    qa_source_compressed_painter *painter,
    const qa_source_paint_sound *sound,
    const qa_source_paint_channel *channel, int32_t *paint,
    size_t paint_length, int64_t count, double offset_value,
    int64_t buffer_offset, int64_t volume) {
  qa_source_paint_chunk *current = chunk_or_fail(sound->sound_data);
  int64_t index = 0;
  int64_t sample_offset;
  if (channel->doppler)
    sample_offset =
        (int64_t)trunc((double)(float)((float)offset_value *
                                        (float)channel->old_doppler_scale));
  else
    sample_offset = (int64_t)offset_value;
  while (sample_offset >= 4096) {
    current = chunk_or_fail(current->next);
    sample_offset -= 4096;
    index++;
  }
  if (index != painter->scratch_index || painter->scratch_sound != sound) {
    decode_current_adpcm(current, painter->scratch);
    painter->scratch_index = index;
    painter->scratch_sound = sound;
  }
  for (int64_t i = 0; i < count; i++) {
    painter_add(painter, paint, paint_length, buffer_offset + i,
                scratch_at(painter->scratch, sample_offset++), channel,
                volume);
    if (sample_offset == 4096) {
      current = chunk_or_fail(current->next);
      decode_current_adpcm(current, painter->scratch);
      sample_offset = 0;
      painter->scratch_index++;
    }
  }
}

void qa_source_compressed_painter_paint_wavelet(
    qa_source_compressed_painter *painter,
    const qa_source_paint_sound *sound,
    const qa_source_paint_channel *channel, int32_t *paint,
    size_t paint_length, int64_t count, int64_t sample_offset,
    int64_t buffer_offset, int64_t volume) {
  qa_source_paint_chunk *current = chunk_or_fail(sound->sound_data);
  int64_t index = 0;
  while (sample_offset >= 2048) {
    current = chunk_or_fail(current->next);
    sample_offset -= 2048;
    index++;
  }
  if (index != painter->scratch_index || painter->scratch_sound != sound) {
    /* TS intentionally calls ADPCM here, wavelet only at the boundary. */
    decode_current_adpcm(current, painter->scratch);
    painter->scratch_index = index;
    painter->scratch_sound = sound;
  }
  for (int64_t i = 0; i < count; i++) {
    painter_add(painter, paint, paint_length, buffer_offset + i,
                scratch_at(painter->scratch, sample_offset++), channel,
                volume);
    if (sample_offset == 2048) {
      current = chunk_or_fail(current->next);
      qa_wavelet_sound_chunk view =
          qa_source_paint_chunk_wavelet_view(current);
      qa_source_wavelet_codec_decode_wavelet(painter->codec, &view,
                                             painter->scratch, 4096);
      painter->scratch_index++;
      sample_offset = 0;
    }
  }
}

void qa_source_compressed_painter_paint_mu_law(
    qa_source_compressed_painter *painter,
    const qa_source_paint_sound *sound,
    const qa_source_paint_channel *channel, int32_t *paint,
    size_t paint_length, int64_t count, int64_t sample_offset,
    int64_t buffer_offset, int64_t volume) {
  qa_source_paint_chunk *current = chunk_or_fail(sound->sound_data);
  while (sample_offset >= 2048) {
    current = current->next != NULL ? current->next
                                     : chunk_or_fail(sound->sound_data);
    sample_offset -= 2048;
  }
  if (!channel->doppler) {
    for (int64_t i = 0; i < count; i++) {
      uint8_t byte = load_u8(current->data, QA_ADPCM_CHUNK_BYTES, sample_offset++);
      painter_add(painter, paint, paint_length, buffer_offset + i,
                  qa_source_wavelet_codec_mu_law_sample(painter->codec, byte),
                  channel, volume);
      if (sample_offset == 2048) {
        current = chunk_or_fail(current->next);
        sample_offset = 0;
      }
    }
  } else {
    float offset = (float)sample_offset;
    for (int64_t i = 0; i < count; i++) {
      uint8_t byte = load_u8(current->data, QA_ADPCM_CHUNK_BYTES,
                             (int64_t)trunc(offset));
      int64_t sample =
          qa_source_wavelet_codec_mu_law_sample(painter->codec, byte);
      offset = (float)(offset + (float)channel->doppler_scale);
      painter_add(painter, paint, paint_length, buffer_offset + i, sample,
                  channel, volume);
      if (offset >= 2048) {
        current = current->next != NULL ? current->next
                                         : chunk_or_fail(sound->sound_data);
        offset = 0;
      }
    }
  }
}
