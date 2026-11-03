/* Port of src/audio/streams.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/audio/streams.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "qa/foundation/support.h"
#include "qa/platform/vorbis.h"

static noreturn void plain_error(const char *format, ...) {
  char message[512];
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof message, format, args);
  va_end(args);
  qa_raise("Error", 0, 0, "%s", message);
}

qa_pcm_stream *qa_pcm_stream_create(void *state, const qa_pcm_stream_ops *ops) {
  qa_pcm_stream *stream = malloc(sizeof *stream);
  if (stream == NULL) return NULL;
  stream->state = state;
  stream->ops = ops;
  return stream;
}

int64_t qa_pcm_stream_sample_rate(const qa_pcm_stream *stream) {
  return stream->ops->sample_rate(stream->state);
}
int qa_pcm_stream_channels(const qa_pcm_stream *stream) {
  return stream->ops->channels(stream->state);
}
int64_t qa_pcm_stream_frame_count(const qa_pcm_stream *stream) {
  return stream->ops->frame_count(stream->state);
}
int64_t qa_pcm_stream_position_frames(const qa_pcm_stream *stream) {
  return stream->ops->position_frames(stream->state);
}
bool qa_pcm_stream_read(qa_pcm_stream *stream, int64_t max_frames,
                        qa_pcm_sound *out, bool *has) {
  return stream->ops->read(stream->state, max_frames, out, has);
}
void qa_pcm_stream_seek(qa_pcm_stream *stream, int64_t frame) {
  stream->ops->seek(stream->state, frame);
}
void qa_pcm_stream_close(qa_pcm_stream *stream) {
  if (stream == NULL) return;
  stream->ops->close(stream->state);
  free(stream);
}

/* ---- MemoryPcmStream ---- */

typedef struct memory_state {
  const qa_pcm_sound *pcm;
  qa_pcm_sound *owned_pcm; /* freed on close when set (openPcmBytes) */
  int64_t position;
  bool closed;
} memory_state;

static void memory_check(const memory_state *state) {
  if (state->closed) plain_error("PCM stream closed");
}
static int64_t memory_sample_rate(void *state) {
  return ((memory_state *)state)->pcm->sample_rate;
}
static int memory_channels(void *state) {
  return ((memory_state *)state)->pcm->channels;
}
static int64_t memory_frame_count(void *state) {
  return ((memory_state *)state)->pcm->frame_count;
}
static int64_t memory_position(void *state) {
  return ((memory_state *)state)->position;
}
static bool memory_read(void *opaque, int64_t max_frames, qa_pcm_sound *out,
                        bool *has) {
  memory_state *state = opaque;
  memory_check(state);
  if (max_frames < 1) qa_range_error("Invalid PCM frame request");
  int64_t remaining = state->pcm->frame_count - state->position;
  int64_t count = max_frames < remaining ? max_frames : remaining;
  *has = count != 0;
  if (count == 0) return true;
  memset(out, 0, sizeof *out);
  out->samples =
      state->pcm->samples + (size_t)(state->position * state->pcm->channels);
  out->samples_owned = false;
  out->sample_count = (size_t)(count * state->pcm->channels);
  out->channels = state->pcm->channels;
  out->sample_rate = state->pcm->sample_rate;
  out->frame_count = count;
  out->loop_start = -1;
  state->position += count;
  return true;
}
static void memory_seek(void *opaque, int64_t frame) {
  memory_state *state = opaque;
  memory_check(state);
  if (frame < 0 || frame > state->pcm->frame_count)
    qa_range_error("PCM seek outside stream");
  state->position = frame;
}
static void memory_close(void *opaque) {
  memory_state *state = opaque;
  state->closed = true;
  if (state->owned_pcm != NULL) {
    qa_pcm_sound_dispose(state->owned_pcm);
    free(state->owned_pcm);
  }
  free(state);
}
static const qa_pcm_stream_ops memory_ops = {
    memory_sample_rate, memory_channels, memory_frame_count,
    memory_position,    memory_read,     memory_seek,
    memory_close,
};

qa_pcm_stream *qa_memory_pcm_stream_create(const qa_pcm_sound *pcm) {
  memory_state *state = calloc(1, sizeof *state);
  if (state == NULL) return NULL;
  state->pcm = pcm;
  qa_pcm_stream *stream = qa_pcm_stream_create(state, &memory_ops);
  if (stream == NULL) free(state);
  return stream;
}

/* ---- VorbisPcmStream ---- */

typedef struct vorbis_state {
  qa_vorbis_decoder decoder;
  char *temporary_directory; /* owned or NULL */
  bool closed;
  /* Quiet decode (qa_try_decode_sound_bytes): read failures set
   * failed instead of aborting, as the TS catch would see them. */
  bool quiet;
  bool failed;
} vorbis_state;

static noreturn void vorbis_fail(const qa_vorbis_error *error) {
  fprintf(stderr, "VorbisError: %s\n", error->message);
  abort();
}

static int64_t vorbis_sample_rate(void *opaque) {
  return ((vorbis_state *)opaque)->decoder.metadata.sample_rate;
}
static int vorbis_channels(void *opaque) {
  return ((vorbis_state *)opaque)->decoder.metadata.channels;
}
static int64_t vorbis_frame_count(void *opaque) {
  return ((vorbis_state *)opaque)->decoder.metadata.total_frames;
}
static int64_t vorbis_position(void *opaque) {
  vorbis_state *state = opaque;
  qa_vorbis_error error;
  int64_t position = 0;
  if (!qa_vorbis_decoder_position_frames(&state->decoder, &position, &error))
    vorbis_fail(&error);
  return position;
}
static bool vorbis_read(void *opaque, int64_t max_frames, qa_pcm_sound *out,
                        bool *has) {
  vorbis_state *state = opaque;
  qa_vorbis_error error;
  qa_vorbis_pcm_chunk chunk;
  memset(&chunk, 0, sizeof chunk);
  if (!qa_vorbis_decoder_read(&state->decoder, max_frames, &chunk, has,
                              &error)) {
    if (state->quiet) {
      state->failed = true;
      *has = false;
      return true;
    }
    vorbis_fail(&error);
  }
  if (!*has) return true;
  memset(out, 0, sizeof *out);
  out->samples = chunk.samples;
  out->samples_owned = true;
  out->sample_count = chunk.sample_count;
  out->frame_count = chunk.frames;
  out->sample_rate = chunk.sample_rate;
  out->channels = chunk.channels;
  out->loop_start = -1;
  return true;
}
static void vorbis_seek(void *opaque, int64_t frame) {
  vorbis_state *state = opaque;
  qa_vorbis_error error;
  if (!qa_vorbis_decoder_seek(&state->decoder, frame, &error))
    vorbis_fail(&error);
}
static void vorbis_close(void *opaque) {
  vorbis_state *state = opaque;
  if (state->closed) {
    free(state);
    return;
  }
  state->closed = true;
  qa_vorbis_error error;
  qa_vorbis_decoder_close(&state->decoder, &error);
  if (state->temporary_directory != NULL) {
    char path[1024];
    snprintf(path, sizeof path, "%s/source.ogg", state->temporary_directory);
    remove(path);
    rmdir(state->temporary_directory);
    free(state->temporary_directory);
  }
  free(state);
}
static const qa_pcm_stream_ops vorbis_ops = {
    vorbis_sample_rate, vorbis_channels, vorbis_frame_count,
    vorbis_position,    vorbis_read,     vorbis_seek,
    vorbis_close,
};

static qa_pcm_stream *vorbis_wrap(vorbis_state *state) {
  qa_pcm_stream *stream = qa_pcm_stream_create(state, &vorbis_ops);
  if (stream == NULL) vorbis_close(state);
  return stream;
}

qa_pcm_stream *qa_vorbis_pcm_stream_open(const char *path) {
  vorbis_state *state = calloc(1, sizeof *state);
  if (state == NULL) return NULL;
  qa_vorbis_error error;
  if (!qa_vorbis_decoder_open(&state->decoder, path, &error)) {
    free(state);
    vorbis_fail(&error);
  }
  return vorbis_wrap(state);
}

/* quiet reports opening failures in *result instead of aborting,
 * for the qa_try_* entry points (the bank's TS catch). A stream it
 * returns is ordinary unless keep_quiet keeps reads quiet too. */
static qa_pcm_stream *vorbis_from_bytes_impl(const uint8_t *bytes,
                                             size_t length, bool quiet,
                                             bool keep_quiet,
                                             qa_pcm_try_result *result) {
  *result = QA_PCM_TRY_OOM;
  const char *tmp = getenv("TMPDIR");
  if (tmp == NULL || tmp[0] == '\0') tmp = "/tmp";
  char directory[1024];
  snprintf(directory, sizeof directory, "%s/quake-audio-XXXXXX", tmp);
  if (mkdtemp(directory) == NULL) {
    if (quiet) {
      *result = QA_PCM_TRY_INVALID;
      return NULL;
    }
    plain_error("could not create temporary audio directory");
  }
  vorbis_state *state = calloc(1, sizeof *state);
  if (state == NULL) {
    rmdir(directory);
    return NULL;
  }
  char path[2048];
  snprintf(path, sizeof path, "%s/source.ogg", directory);
  FILE *file = fopen(path, "wb");
  bool wrote = file != NULL && fwrite(bytes, 1, length, file) == length;
  if (file != NULL) fclose(file);
  qa_vorbis_error error;
  bool opened =
      wrote && qa_vorbis_decoder_open(&state->decoder, path, &error);
  if (!opened) {
    remove(path);
    rmdir(directory);
    free(state);
    if (quiet) {
      *result = QA_PCM_TRY_INVALID;
      return NULL;
    }
    if (!wrote) plain_error("could not write temporary audio file");
    vorbis_fail(&error);
  }
  /* The open descriptor keeps the data alive on POSIX, so the temporary
     file and directory go now and an abnormal exit leaves nothing behind
     (TS: rmSync(directory) except on win32). */
  remove(path);
  rmdir(directory);
  state->temporary_directory = NULL;
  state->quiet = keep_quiet;
  qa_pcm_stream *stream = vorbis_wrap(state);
  if (stream != NULL) *result = QA_PCM_TRY_OK;
  return stream;
}

qa_pcm_stream *qa_vorbis_pcm_stream_from_bytes(const uint8_t *bytes,
                                               size_t length) {
  qa_pcm_try_result result;
  return vorbis_from_bytes_impl(bytes, length, false, false, &result);
}

static bool is_ogg(const uint8_t *bytes, size_t length) {
  return length >= 4 && bytes[0] == 79 && bytes[1] == 103 && bytes[2] == 103 &&
         bytes[3] == 83;
}

/* TS MemoryPcmStream holds the decoded sound; the C stream owns a
 * heap copy, freed by memory_close. Takes over decoded.pcm. */
static qa_pcm_stream *wrap_decoded_wav(qa_decoded_wav *decoded) {
  qa_pcm_sound *owned = malloc(sizeof *owned);
  if (owned == NULL) {
    qa_decoded_wav_dispose(decoded);
    return NULL;
  }
  *owned = decoded->pcm;
  memory_state *state = calloc(1, sizeof *state);
  if (state == NULL) {
    qa_pcm_sound_dispose(owned);
    free(owned);
    return NULL;
  }
  state->pcm = owned;
  state->owned_pcm = owned;
  qa_pcm_stream *stream = qa_pcm_stream_create(state, &memory_ops);
  if (stream == NULL) {
    qa_pcm_sound_dispose(owned);
    free(owned);
    free(state);
    return NULL;
  }
  return stream;
}

qa_pcm_stream *qa_open_pcm_bytes(const uint8_t *bytes, size_t length,
                                 const char *source) {
  if (is_ogg(bytes, length))
    return qa_vorbis_pcm_stream_from_bytes(bytes, length);
  qa_decoded_wav decoded;
  if (!qa_decode_wav(bytes, length, source != NULL ? source : "<sound>",
                     &decoded))
    return NULL;
  return wrap_decoded_wav(&decoded);
}

qa_pcm_try_result qa_try_open_pcm_bytes(const uint8_t *bytes, size_t length,
                                        const char *source,
                                        qa_pcm_stream **out) {
  *out = NULL;
  if (is_ogg(bytes, length)) {
    qa_pcm_try_result result;
    *out = vorbis_from_bytes_impl(bytes, length, true, false, &result);
    return result;
  }
  qa_decoded_wav decoded;
  qa_wav_try_result decoded_result = qa_try_decode_wav(
      bytes, length, source != NULL ? source : "<sound>", &decoded);
  if (decoded_result == QA_WAV_TRY_INVALID) return QA_PCM_TRY_INVALID;
  if (decoded_result == QA_WAV_TRY_OOM) return QA_PCM_TRY_OOM;
  *out = wrap_decoded_wav(&decoded);
  return *out != NULL ? QA_PCM_TRY_OK : QA_PCM_TRY_OOM;
}

/* Reads a Vorbis stream to completion, as decodeSoundBytes does.
 * Takes over the stream. QA_PCM_TRY_INVALID only for a quiet stream
 * whose read failed (the aborting stream never returns it). */
static qa_pcm_try_result decode_vorbis_stream(qa_pcm_stream *stream,
                                              qa_pcm_sound *out) {
  vorbis_state *state = stream->state;
  int channels = qa_pcm_stream_channels(stream);
  int64_t rate = qa_pcm_stream_sample_rate(stream);
  int64_t total = qa_pcm_stream_frame_count(stream) * channels;
  int16_t *samples = NULL;
  if (total > 0) {
    samples = malloc((size_t)total * sizeof *samples);
    if (samples == NULL) {
      qa_pcm_stream_close(stream);
      return QA_PCM_TRY_OOM;
    }
  }
  size_t offset = 0;
  for (;;) {
    qa_pcm_sound chunk;
    bool has = false;
    if (!qa_pcm_stream_read(stream, 16384, &chunk, &has)) {
      free(samples);
      qa_pcm_stream_close(stream);
      return QA_PCM_TRY_OOM;
    }
    if (!has) break;
    memcpy(samples + offset, chunk.samples,
           chunk.sample_count * sizeof *samples);
    offset += chunk.sample_count;
    qa_pcm_sound_dispose(&chunk);
  }
  bool failed = state->failed;
  qa_pcm_stream_close(stream);
  if (failed) {
    free(samples);
    memset(out, 0, sizeof *out);
    return QA_PCM_TRY_INVALID;
  }
  memset(out, 0, sizeof *out);
  out->samples = samples;
  out->samples_owned = true;
  out->sample_count = offset;
  out->channels = channels;
  out->sample_rate = rate;
  out->frame_count = (int64_t)(offset / (size_t)channels);
  out->loop_start = -1;
  return QA_PCM_TRY_OK;
}

bool qa_decode_sound_bytes(const uint8_t *bytes, size_t length,
                           const char *source, qa_pcm_sound *out) {
  if (!is_ogg(bytes, length)) {
    qa_decoded_wav decoded;
    if (!qa_decode_wav(bytes, length, source != NULL ? source : "<sound>",
                       &decoded))
      return false;
    *out = decoded.pcm;
    return true;
  }
  qa_pcm_stream *stream = qa_vorbis_pcm_stream_from_bytes(bytes, length);
  if (stream == NULL) return false;
  return decode_vorbis_stream(stream, out) == QA_PCM_TRY_OK;
}

qa_pcm_try_result qa_try_decode_sound_bytes(const uint8_t *bytes,
                                            size_t length, const char *source,
                                            qa_pcm_sound *out) {
  if (!is_ogg(bytes, length)) {
    qa_decoded_wav decoded;
    qa_wav_try_result result = qa_try_decode_wav(
        bytes, length, source != NULL ? source : "<sound>", &decoded);
    if (result == QA_WAV_TRY_INVALID) return QA_PCM_TRY_INVALID;
    if (result == QA_WAV_TRY_OOM) return QA_PCM_TRY_OOM;
    *out = decoded.pcm;
    return QA_PCM_TRY_OK;
  }
  qa_pcm_try_result result;
  qa_pcm_stream *stream =
      vorbis_from_bytes_impl(bytes, length, true, true, &result);
  if (stream == NULL) {
    memset(out, 0, sizeof *out);
    return result;
  }
  return decode_vorbis_stream(stream, out);
}

/* ---- RawAudioStream ---- */

void qa_raw_audio_stream_init(qa_raw_audio_stream *stream,
                              int64_t output_rate) {
  memset(stream, 0, sizeof *stream);
  stream->output_rate = output_rate;
  stream->channels = 2;
}

void qa_raw_audio_stream_destroy(qa_raw_audio_stream *stream) {
  for (size_t i = 0; i < stream->segment_count; i++)
    free(stream->segments[i].samples);
  free(stream->segments);
  memset(stream, 0, sizeof *stream);
}

bool qa_raw_audio_stream_initialized(const qa_raw_audio_stream *stream) {
  return stream->input_rate != 0;
}

int64_t qa_raw_audio_stream_source_position(
    const qa_raw_audio_stream *stream) {
  return (int64_t)floor(stream->origin + (double)stream->output_frames *
                                             (double)stream->input_rate /
                                             (double)stream->output_rate);
}

int64_t qa_raw_audio_stream_queued_source_frames(
    const qa_raw_audio_stream *stream) {
  int64_t queued = stream->end - qa_raw_audio_stream_source_position(stream);
  return queued > 0 ? queued : 0;
}

static bool copy_segment(const qa_raw_segment *from, qa_raw_segment *to) {
  to->begin = from->begin;
  to->end = from->end;
  to->sample_count = from->sample_count;
  to->samples = NULL;
  if (from->sample_count > 0) {
    to->samples = malloc(from->sample_count * sizeof *to->samples);
    if (to->samples == NULL) return false;
    memcpy(to->samples, from->samples,
           from->sample_count * sizeof *to->samples);
  }
  return true;
}

bool qa_raw_audio_stream_with_output_rate(const qa_raw_audio_stream *stream,
                                          int64_t output_rate,
                                          qa_raw_audio_stream *out) {
  qa_raw_audio_stream_init(out, output_rate);
  if (stream->segment_count > 0) {
    out->segments = calloc(stream->segment_count, sizeof *out->segments);
    if (out->segments == NULL) return false;
    out->segment_capacity = stream->segment_count;
    for (size_t i = 0; i < stream->segment_count; i++) {
      if (!copy_segment(&stream->segments[i], &out->segments[i])) {
        out->segment_count = i;
        qa_raw_audio_stream_destroy(out);
        return false;
      }
      out->segment_count = i + 1;
    }
  }
  out->input_rate = stream->input_rate;
  out->channels = stream->channels;
  out->origin = stream->origin + (double)stream->output_frames *
                                     (double)stream->input_rate /
                                     (double)stream->output_rate;
  out->end = stream->end;
  out->paused = stream->paused;
  return true;
}

bool qa_raw_audio_stream_queue(qa_raw_audio_stream *stream,
                               const qa_stream_pcm *chunk) {
  if (chunk->sample_rate < 1 || chunk->source_sample < 0 ||
      (chunk->channels != 1 && chunk->channels != 2) ||
      chunk->sample_count % (size_t)chunk->channels != 0)
    qa_range_error("Invalid streamed PCM");
  if (chunk->reset_stream || stream->input_rate == 0) {
    for (size_t i = 0; i < stream->segment_count; i++)
      free(stream->segments[i].samples);
    stream->segment_count = 0;
    stream->input_rate = chunk->sample_rate;
    stream->channels = chunk->channels;
    stream->origin = (double)chunk->source_sample;
    stream->output_frames = 0;
    stream->end = chunk->source_sample;
  }
  if (chunk->sample_rate != stream->input_rate ||
      chunk->channels != stream->channels)
    plain_error("PCM stream format changed without reset");
  if (chunk->source_sample != stream->end)
    plain_error("PCM source discontinuity: expected %lld, received %lld",
                (long long)stream->end, (long long)chunk->source_sample);
  int16_t *samples = NULL;
  if (chunk->sample_count > 0) {
    samples = malloc(chunk->sample_count * sizeof *samples);
    if (samples == NULL) return false;
    if (!chunk->is_u8) {
      memcpy(samples, chunk->samples_s16,
             chunk->sample_count * sizeof *samples);
    } else {
      for (size_t i = 0; i < chunk->sample_count; i++)
        samples[i] = (int16_t)((chunk->samples_u8[i] - 128) * 256); /* JS `<< 8` on int32; a C shift of a negative value is undefined */
    }
  }
  int64_t end =
      chunk->source_sample + (int64_t)(chunk->sample_count / (size_t)chunk->channels);
  if (end > chunk->source_sample) {
    if (stream->segment_count == stream->segment_capacity) {
      size_t capacity = stream->segment_capacity == 0
                            ? 4
                            : stream->segment_capacity * 2;
      qa_raw_segment *grown =
          realloc(stream->segments, capacity * sizeof *grown);
      if (grown == NULL) {
        free(samples);
        return false;
      }
      stream->segments = grown;
      stream->segment_capacity = capacity;
    }
    qa_raw_segment *segment = &stream->segments[stream->segment_count++];
    segment->begin = chunk->source_sample;
    segment->end = end;
    segment->samples = samples;
    segment->sample_count = chunk->sample_count;
  } else {
    free(samples);
  }
  stream->end = end;
  return true;
}

void qa_raw_audio_stream_mix(qa_raw_audio_stream *stream, int64_t frames,
                             double gain, qa_raw_refill_fn refill,
                             void *refill_ctx, double *output) {
  for (int64_t i = 0; i < frames * 2; i++) output[i] = 0;
  if (stream->paused) return;
  for (int64_t frame = 0; frame < frames; frame++) {
    for (;;) {
      int64_t position = qa_raw_audio_stream_source_position(stream);
      if (stream->segment_count > 0 && position < stream->segments[0].end)
        break;
      if (stream->segment_count > 0) {
        free(stream->segments[0].samples);
        memmove(stream->segments, stream->segments + 1,
                (stream->segment_count - 1) * sizeof *stream->segments);
        stream->segment_count--;
      }
      if (stream->segment_count == 0) {
        qa_stream_pcm chunk;
        if (refill == NULL || !refill(refill_ctx, &chunk)) return;
        if (!qa_raw_audio_stream_queue(stream, &chunk)) abort(); /* OOM */
      }
    }
    qa_raw_segment *segment = &stream->segments[0];
    int64_t position = qa_raw_audio_stream_source_position(stream);
    int64_t index = (position - segment->begin) * stream->channels;
    if (index < 0 || index >= (int64_t)segment->sample_count ||
        (stream->channels == 2 && index + 1 >= (int64_t)segment->sample_count))
      plain_error("PCM source position outside queued segment");
    double left = segment->samples[index];
    double right = stream->channels == 1 ? left : segment->samples[index + 1];
    output[frame * 2] = left * gain;
    output[frame * 2 + 1] = right * gain;
    stream->output_frames++;
  }
}

qa_save_value *qa_raw_audio_stream_capture_checkpoint(
    const qa_raw_audio_stream *stream) {
  qa_save_value *root = qa_save_value_object();
  if (root == NULL) return NULL;
  qa_save_value *segments = qa_save_value_array();
  if (segments == NULL) {
    qa_save_value_free(root);
    return NULL;
  }
  for (size_t i = 0; i < stream->segment_count; i++) {
    const qa_raw_segment *segment = &stream->segments[i];
    qa_save_value *entry = qa_save_value_object();
    qa_save_value *samples = qa_save_value_array();
    bool ok = entry != NULL && samples != NULL;
    for (size_t j = 0; ok && j < segment->sample_count; j++) {
      qa_save_value *number = qa_save_value_number(segment->samples[j]);
      ok = number != NULL && qa_save_value_push(samples, number);
      if (number != NULL && !ok) qa_save_value_free(number);
    }
    qa_save_value *begin = qa_save_value_number((double)segment->begin);
    qa_save_value *end = qa_save_value_number((double)segment->end);
    ok = ok && begin != NULL && end != NULL &&
         qa_save_value_set(entry, "begin", begin) &&
         qa_save_value_set(entry, "end", end) &&
         qa_save_value_set(entry, "samples", samples) &&
         qa_save_value_push(segments, entry);
    if (!ok) {
      qa_save_value_free(begin);
      qa_save_value_free(end);
      qa_save_value_free(samples);
      qa_save_value_free(entry);
      qa_save_value_free(segments);
      qa_save_value_free(root);
      return NULL;
    }
  }
  qa_save_value * value = qa_save_value_number((double)stream->output_rate);
  qa_save_value * value2 = qa_save_value_number((double)stream->input_rate);
  qa_save_value * value3 = qa_save_value_number((double)stream->channels);
  qa_save_value * value4 = qa_save_value_number(stream->origin);
  qa_save_value * value5 = qa_save_value_number((double)stream->output_frames);
  qa_save_value * value6 = qa_save_value_number((double)stream->end);
  qa_save_value * value7 = qa_save_value_boolean(stream->paused);
  qa_save_value *values[7] = {
      value,
      value2,
      value3,
      value4,
      value5,
      value6,
      value7,
  };
  static const char *const keys[7] = {"outputRate", "inputRate", "channels",
                                      "origin", "outputFrames", "end",
                                      "paused"};
  bool ok = true;
  for (int i = 0; i < 7; i++) {
    ok = ok && values[i] != NULL;
    if (ok && !qa_save_value_set(root, keys[i], values[i])) {
      qa_save_value_free(values[i]);
      ok = false;
    }
  }
  if (ok && !qa_save_value_set(root, "segments", segments)) ok = false;
  if (!ok) {
    qa_save_value_free(segments);
    qa_save_value_free(root);
    return NULL;
  }
  return root;
}

static bool reader_field_number(const qa_save_reader *reader, const char *name,
                                qa_save_reader *out) {
  return qa_save_reader_field(reader, name, out);
}

bool qa_raw_audio_stream_restore_checkpoint(qa_raw_audio_stream *out,
                                            const qa_save_value *value,
                                            int64_t output_rate) {
  qa_save_reader root;
  if (!qa_save_reader_init(&root, value, "raw-pcm")) return false;
  bool ok = false;
  qa_raw_audio_stream stream;
  qa_save_reader field;
  int64_t rate = 0;
  if (!reader_field_number(&root, "outputRate", &field)) goto done_root;
  rate = (int64_t)qa_save_reader_integer_min(&field, 1);
  qa_save_reader_destroy(&field);
  qa_raw_audio_stream_init(&stream, rate);
  if (!qa_save_reader_field(&root, "inputRate", &field)) goto done;
  stream.input_rate = (int64_t)qa_save_reader_integer_min(&field, 0);
  qa_save_reader_destroy(&field);
  if (!qa_save_reader_field(&root, "channels", &field)) goto done;
  {
    const double choices[2] = {1, 2};
    stream.channels = (int)qa_save_reader_choice_number(&field, choices, 2);
  }
  qa_save_reader_destroy(&field);
  if (!qa_save_reader_field(&root, "origin", &field)) goto done;
  stream.origin = qa_save_reader_finite(&field);
  qa_save_reader_destroy(&field);
  if (!qa_save_reader_field(&root, "outputFrames", &field)) goto done;
  stream.output_frames = (int64_t)qa_save_reader_integer_min(&field, 0);
  qa_save_reader_destroy(&field);
  if (!qa_save_reader_field(&root, "end", &field)) goto done;
  stream.end = (int64_t)qa_save_reader_integer_min(&field, 0);
  qa_save_reader_destroy(&field);
  if (!qa_save_reader_field(&root, "paused", &field)) goto done;
  stream.paused = qa_save_reader_boolean(&field);
  qa_save_reader_destroy(&field);
  if (!qa_save_reader_field(&root, "segments", &field)) goto done;
  {
    size_t count = qa_save_reader_list_count(&field);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader item, sub;
      if (!qa_save_reader_item(&field, i, &item)) {
        qa_save_reader_destroy(&field);
        goto done;
      }
      if (!qa_save_reader_field(&item, "begin", &sub)) {
        qa_save_reader_destroy(&item);
        qa_save_reader_destroy(&field);
        goto done;
      }
      int64_t begin = (int64_t)qa_save_reader_integer_min(&sub, 0);
      qa_save_reader_destroy(&sub);
      if (!qa_save_reader_field(&item, "end", &sub)) {
        qa_save_reader_destroy(&item);
        qa_save_reader_destroy(&field);
        goto done;
      }
      int64_t end = (int64_t)qa_save_reader_integer_min(&sub, (double)begin);
      qa_save_reader_destroy(&sub);
      if (!qa_save_reader_field(&item, "samples", &sub)) {
        qa_save_reader_destroy(&item);
        qa_save_reader_destroy(&field);
        goto done;
      }
      size_t sample_count = qa_save_reader_list_count(&sub);
      int16_t *samples = NULL;
      if (sample_count > 0) {
        samples = malloc(sample_count * sizeof *samples);
        if (samples == NULL) {
          qa_save_reader_destroy(&sub);
          qa_save_reader_destroy(&item);
          qa_save_reader_destroy(&field);
          goto done;
        }
      }
      for (size_t j = 0; j < sample_count; j++) {
        qa_save_reader sample_reader;
        if (!qa_save_reader_item(&sub, j, &sample_reader)) {
          free(samples);
          qa_save_reader_destroy(&sub);
          qa_save_reader_destroy(&item);
          qa_save_reader_destroy(&field);
          goto done;
        }
        double sample = qa_save_reader_integer_min(&sample_reader, -32768);
        if (sample > 32767)
          qa_save_reader_fail(&sample_reader, "sample exceeds signed PCM");
        samples[j] = (int16_t)sample;
        qa_save_reader_destroy(&sample_reader);
      }
      qa_save_reader_destroy(&sub);
      if (sample_count != (size_t)((end - begin) * stream.channels) ||
          begin == end) {
        free(samples);
        qa_save_reader_fail(&item, "invalid PCM segment extent");
      }
      if (stream.segment_count == stream.segment_capacity) {
        size_t capacity = stream.segment_capacity == 0
                              ? 4
                              : stream.segment_capacity * 2;
        qa_raw_segment *grown =
            realloc(stream.segments, capacity * sizeof *grown);
        if (grown == NULL) {
          free(samples);
          qa_save_reader_destroy(&item);
          qa_save_reader_destroy(&field);
          goto done;
        }
        stream.segments = grown;
        stream.segment_capacity = capacity;
      }
      qa_raw_segment *segment = &stream.segments[stream.segment_count++];
      segment->begin = begin;
      segment->end = end;
      segment->samples = samples;
      segment->sample_count = sample_count;
      qa_save_reader_destroy(&item);
    }
  }
  qa_save_reader_destroy(&field);
  for (size_t i = 1; i < stream.segment_count; i++)
    if (stream.segments[i - 1].end != stream.segments[i].begin)
      qa_save_reader_fail(&root, "discontinuous PCM segments");
  {
    bool invalid = stream.origin < 0 ||
                   (stream.segment_count > 0 &&
                    qa_raw_audio_stream_source_position(&stream) <
                        stream.segments[0].begin) ||
                   (stream.segment_count > 0 &&
                    stream.segments[stream.segment_count - 1].end != stream.end) ||
                   (stream.input_rate == 0 && stream.segment_count != 0);
    if (invalid) qa_save_reader_fail(&root, "invalid PCM cursor");
  }
  if (output_rate < 1) qa_range_error("Invalid PCM output rate");
  if (rate == output_rate) {
    *out = stream;
  } else if (!qa_raw_audio_stream_with_output_rate(&stream, output_rate, out)) {
    goto done;
  } else {
    qa_raw_audio_stream_destroy(&stream);
  }
  ok = true;
done:
  if (!ok) qa_raw_audio_stream_destroy(&stream);
done_root:
  qa_save_reader_destroy(&root);
  return ok;
}
