/* Port of src/platform/vorbis.ts. GPL-2.0-or-later. */
#include "qa/platform/vorbis.h"

#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

/* TS `throw new Error(msg)`, as in src/text/atlas.c. */
static noreturn void qa_error(const char *format, ...) {
  va_list args;
  va_start(args, format);
  fputs("Error: ", stderr);
  vfprintf(stderr, format, args);
  fputc('\n', stderr);
  va_end(args);
  abort();
}

static bool set_vorbis_error(qa_vorbis_error *error, const char *operation, int code) {
  if (error != NULL) {
    snprintf(error->operation, sizeof error->operation, "%s", operation);
    error->code = code;
    snprintf(error->message, sizeof error->message,
             "%s failed with Vorbis error %d", operation, code);
  }
  return false;
}

static bool set_plain_error(qa_vorbis_error *error, const char *message) {
  if (error != NULL) {
    error->operation[0] = '\0';
    error->code = 0;
    snprintf(error->message, sizeof error->message, "%s", message);
  }
  return false;
}

static bool safe_count(int64_t value, const char *name, int64_t *out,
                       qa_vorbis_error *error) {
  if (value < 0) return set_vorbis_error(error, name, (int)value);
  if (value > QA_MAX_SAFE_INTEGER)
    qa_range_error("%s exceeds safe integer precision", name);
  *out = value;
  return true;
}

static bool stream_format(OggVorbis_File *file, int stream, int64_t *out_rate,
                          int *out_channels, qa_vorbis_error *error) {
  vorbis_info *info = ov_info(file, stream);
  if (info == NULL) return set_plain_error(error, "ov_info returned no stream metadata");
  int64_t sample_rate = 0;
  if (!safe_count((int64_t)info->rate, "Vorbis sample rate", &sample_rate, error))
    return false;
  if (info->channels != 1 && info->channels != 2)
    return set_plain_error(error, "Vorbis output must be mono or stereo");
  if (sample_rate < 8000 || sample_rate > 192000)
    return set_plain_error(error, "Vorbis sample rate must be in 8000..192000 Hz");
  *out_rate = sample_rate;
  *out_channels = info->channels;
  return true;
}

bool qa_vorbis_decoder_open(qa_vorbis_decoder *decoder, const char *path,
                            qa_vorbis_error *error) {
  memset(decoder, 0, sizeof *decoder);
  if (path[0] == '\0')
    qa_error("Vorbis path must be nonempty and contain no NUL");
  int result = ov_fopen(path, &decoder->file);
  /* Failed ov_fopen cleans its partial state; ov_clear is valid only after success. */
  if (result != 0) return set_vorbis_error(error, "ov_fopen", result);
  decoder->opened = true;
  int64_t sample_rate = 0;
  int channels = 0;
  if (!stream_format(&decoder->file, 0, &sample_rate, &channels, error)) {
    ov_clear(&decoder->file);
    decoder->opened = false;
    return false;
  }
  int64_t logical_streams = 0;
  if (!safe_count((int64_t)ov_streams(&decoder->file), "ov_streams",
                  &logical_streams, error) ||
      logical_streams == 0 || logical_streams > 65536) {
    if (logical_streams != 0 || error == NULL || error->message[0] == '\0')
      set_plain_error(error, "Vorbis logical stream count is outside 1..65536");
    ov_clear(&decoder->file);
    decoder->opened = false;
    return false;
  }
  for (int64_t index = 1; index < logical_streams; index++) {
    int64_t next_rate = 0;
    int next_channels = 0;
    if (!stream_format(&decoder->file, (int)index, &next_rate, &next_channels, error) ||
        next_rate != sample_rate || next_channels != channels) {
      if (next_rate != 0 && next_rate != sample_rate)
        set_plain_error(error, "Vorbis chained streams change the PCM format");
      if (next_channels != 0 && next_channels != channels)
        set_plain_error(error, "Vorbis chained streams change the PCM format");
      ov_clear(&decoder->file);
      decoder->opened = false;
      return false;
    }
  }
  int64_t total_frames = 0;
  if (!safe_count((int64_t)ov_pcm_total(&decoder->file, -1), "ov_pcm_total",
                  &total_frames, error)) {
    ov_clear(&decoder->file);
    decoder->opened = false;
    return false;
  }
  decoder->metadata.sample_rate = sample_rate;
  decoder->metadata.channels = channels;
  decoder->metadata.total_frames = total_frames;
  decoder->metadata.logical_streams = logical_streams;
  decoder->metadata.duration_seconds =
      (double)total_frames / (double)sample_rate;
  return true;
}

static void opened(qa_vorbis_decoder *decoder) {
  if (!decoder->opened) qa_error("Vorbis decoder is closed");
}

bool qa_vorbis_decoder_closed(const qa_vorbis_decoder *decoder) {
  return !decoder->opened;
}

bool qa_vorbis_decoder_position_frames(qa_vorbis_decoder *decoder, int64_t *out,
                                       qa_vorbis_error *error) {
  opened(decoder);
  return safe_count((int64_t)ov_pcm_tell(&decoder->file), "ov_pcm_tell", out, error);
}

bool qa_vorbis_decoder_seek(qa_vorbis_decoder *decoder, int64_t frame,
                            qa_vorbis_error *error) {
  opened(decoder);
  if (frame < 0 || frame > decoder->metadata.total_frames ||
      frame > QA_MAX_SAFE_INTEGER)
    qa_range_error("Vorbis seek frame is outside the stream");
  int result = ov_pcm_seek(&decoder->file, (ogg_int64_t)frame);
  if (result != 0) return set_vorbis_error(error, "ov_pcm_seek", result);
  return true;
}

static int native_big_endian(void) {
  const uint16_t probe = 1;
  return *(const uint8_t *)&probe == 0 ? 1 : 0;
}

bool qa_vorbis_decoder_read(qa_vorbis_decoder *decoder, int64_t max_frames,
                            qa_vorbis_pcm_chunk *out, bool *out_has_chunk,
                            qa_vorbis_error *error) {
  memset(out, 0, sizeof *out);
  *out_has_chunk = false;
  opened(decoder);
  if (max_frames < 1 || max_frames > 1048576)
    qa_range_error("Vorbis read frame limit must be in 1..1048576");
  int channels = decoder->metadata.channels;
  size_t capacity = (size_t)max_frames * (size_t)channels;
  int16_t *samples = (int16_t *)malloc(capacity * sizeof *samples);
  if (samples == NULL) return set_plain_error(error, "out of memory");
  size_t sample_count = 0;
  while (sample_count < capacity) {
    size_t remaining_bytes = (capacity - sample_count) * sizeof *samples;
    int bitstream = 0;
    long count = ov_read(&decoder->file, (char *)(samples + sample_count),
                         (int)remaining_bytes, native_big_endian(), 2, 1,
                         &bitstream);
    if (count < 0) {
      free(samples);
      return set_vorbis_error(error, "ov_read", (int)count);
    }
    if (count == 0) break;
    if ((uint64_t)count > (uint64_t)remaining_bytes ||
        count % (channels * 2) != 0) {
      free(samples);
      qa_error("Vorbis returned an invalid PCM byte count");
    }
    if (bitstream < 0 || (int64_t)bitstream >= decoder->metadata.logical_streams) {
      free(samples);
      qa_error("Vorbis returned an invalid logical stream index");
    }
    sample_count += (size_t)count / 2;
  }
  if (sample_count == 0) {
    free(samples);
    return true;
  }
  out->samples = samples;
  out->sample_count = sample_count;
  out->frames = (int64_t)(sample_count / (size_t)channels);
  out->channels = channels;
  out->sample_rate = decoder->metadata.sample_rate;
  *out_has_chunk = true;
  return true;
}

int qa_vorbis_decoder_close(qa_vorbis_decoder *decoder, qa_vorbis_error *error) {
  if (!decoder->opened) return 0;
  decoder->opened = false;
  int result = ov_clear(&decoder->file);
  if (result != 0) set_vorbis_error(error, "ov_clear", result);
  return result;
}

void qa_vorbis_pcm_chunk_dispose(qa_vorbis_pcm_chunk *chunk) {
  free(chunk->samples);
  chunk->samples = NULL;
  chunk->sample_count = 0;
}
