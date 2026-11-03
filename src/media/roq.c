/* Port of src/media/roq.ts. GPL-2.0-or-later. */
#include "qa/media/roq.h"

#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

/* TS plain `throw new Error(msg)`. */
static noreturn void media_error(const char *format, ...) {
  char text[1536];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof text, format, args);
  va_end(args);
  qa_raise("Error", 0, 0, "%s", text);
}

bool qa_roq_scratch_init(qa_roq_scratch *scratch) {
  memset(scratch, 0, sizeof *scratch);
  scratch->file = (uint8_t *)calloc(QA_ROQ_SCRATCH_FILE_LENGTH, 1);
  scratch->frames = (uint8_t *)calloc(QA_ROQ_SCRATCH_FRAMES_LENGTH, 1);
  if (scratch->file == NULL || scratch->frames == NULL ||
      !qa_source_roq_codebooks_init(&scratch->codebooks)) {
    qa_roq_scratch_destroy(scratch);
    return false;
  }
  scratch->book2 = scratch->codebooks.book2;
  scratch->book4 = scratch->codebooks.book4;
  scratch->book8 = scratch->codebooks.book8;
  return true;
}

void qa_roq_scratch_destroy(qa_roq_scratch *scratch) {
  free(scratch->file);
  free(scratch->frames);
  qa_source_roq_codebooks_destroy(&scratch->codebooks);
  memset(scratch, 0, sizeof *scratch);
}

static bool set_value(qa_save_value *object, const char *key,
                      qa_save_value *child) {
  return child != NULL && qa_save_value_set(object, key, child);
}
static bool set_number(qa_save_value *object, const char *key, double value) {
  return set_value(object, key, qa_save_value_number(value));
}
static bool set_bool(qa_save_value *object, const char *key, bool value) {
  return set_value(object, key, qa_save_value_boolean(value));
}
static bool set_bytes(qa_save_value *object, const char *key,
                      const uint8_t *data, size_t length) {
  return set_value(object, key, qa_save_value_bytes(data, length));
}
static void read_field(const qa_save_reader *reader, const char *name,
                       qa_save_reader *out) {
  if (!qa_save_reader_field(reader, name, out)) {
    media_error("out of memory");
  }
}
static void reader_init(qa_save_reader *reader, const qa_save_value *value,
                        const char *path) {
  if (!qa_save_reader_init(reader, value, path)) {
    media_error("out of memory");
  }
}

qa_save_value *qa_roq_scratch_capture_checkpoint(qa_roq_scratch *scratch) {
  qa_save_value *object = qa_save_value_object();
  if (object == NULL) return NULL;
  bool ok =
      set_bytes(object, "file", scratch->file, QA_ROQ_SCRATCH_FILE_LENGTH) &&
      set_bytes(object, "book2", scratch->book2, 256 * 16 * 4 * 2) &&
      set_bytes(object, "book4", scratch->book4, 256 * 64 * 4 * 2) &&
      set_bytes(object, "book8", scratch->book8, 256 * 256 * 4 * 2) &&
      set_bytes(object, "frames", scratch->frames,
                QA_ROQ_SCRATCH_FRAMES_LENGTH);
  if (!ok) {
    qa_save_value_free(object);
    return NULL;
  }
  return object;
}

void qa_roq_scratch_restore_checkpoint(qa_roq_scratch *scratch,
                                       const qa_save_value *value) {
  qa_save_reader r;
  reader_init(&r, value, "roq-scratch");
  struct {
    const char *name;
    uint8_t *target;
    size_t length;
  } fields[5] = {
      {"file", scratch->file, QA_ROQ_SCRATCH_FILE_LENGTH},
      {"book2", scratch->book2, 256 * 16 * 4 * 2},
      {"book4", scratch->book4, 256 * 64 * 4 * 2},
      {"book8", scratch->book8, 256 * 256 * 4 * 2},
      {"frames", scratch->frames, QA_ROQ_SCRATCH_FRAMES_LENGTH},
  };
  for (int i = 0; i < 5; i++) {
    qa_save_reader f;
    read_field(&r, fields[i].name, &f);
    size_t length = 0;
    qa_save_reader_bytes(&f, &length);
    if (length != fields[i].length) {
      qa_save_reader_fail(&f, "scratch allocation length differs");
    }
    qa_save_reader_destroy(&f);
  }
  for (int i = 0; i < 5; i++) {
    qa_save_reader f;
    read_field(&r, fields[i].name, &f);
    size_t length = 0;
    const uint8_t *bytes = qa_save_reader_bytes(&f, &length);
    memcpy(fields[i].target, bytes, fields[i].length);
    qa_save_reader_destroy(&f);
  }
  qa_save_reader_destroy(&r);
}

void qa_roq_scratch_clear_movie_state(qa_roq_scratch *scratch) {
  memset(scratch->file, 0, QA_ROQ_SCRATCH_FILE_LENGTH);
  memset(scratch->frames, 0, QA_ROQ_SCRATCH_FRAMES_LENGTH);
}

void qa_roq_scratch_clear(qa_roq_scratch *scratch) {
  qa_roq_scratch_clear_movie_state(scratch);
  qa_source_roq_codebooks_clear(&scratch->codebooks);
}

uint8_t *qa_roq_scratch_view(qa_roq_scratch *scratch, int64_t offset,
                             int64_t byte_length) {
  if (offset < 0 || byte_length < 0 ||
      offset + byte_length > QA_ROQ_SCRATCH_FRAMES_LENGTH) {
    qa_range_error("Invalid RoQ physical buffer range");
  }
  return scratch->frames + (size_t)offset;
}

uint8_t *qa_roq_scratch_frame(qa_roq_scratch *scratch, int64_t byte_length,
                              int index) {
  if (byte_length <= 0 || byte_length > 512 * 512 * 4) {
    qa_range_error("Invalid RoQ scratch frame length");
  }
  return qa_roq_scratch_view(scratch, byte_length * index, byte_length);
}

void qa_roq_event_dispose(qa_roq_event *event) {
  free(event->rgba);
  free(event->samples);
  memset(event, 0, sizeof *event);
}

typedef struct vq_reader {
  qa_binary_reader *reader;
  uint32_t codes;
  int remaining;
} vq_reader;

static int vq_code(vq_reader *vq) {
  if (vq->remaining == 0) {
    vq->codes = qa_binary_reader_u16(vq->reader);
    vq->remaining = 8;
  }
  int code = (int)(vq->codes >> 14);
  vq->codes = (vq->codes << 2) & 65535u;
  vq->remaining--;
  return code;
}

static bool decoder_setup(qa_roq_decoder *decoder, const char *source,
                          const qa_roq_decoder_options *options) {
  qa_source_roq_audio_init(&decoder->audio);
  qa_source_roq_audio_setup_table(&decoder->audio);
  if (options != NULL && options->scratch != NULL) {
    decoder->scratch = options->scratch;
    decoder->owns_scratch = false;
  } else {
    if (!qa_roq_scratch_init(&decoder->owned_scratch)) return false;
    decoder->scratch = &decoder->owned_scratch;
    decoder->owns_scratch = true;
  }
  decoder->book2 = decoder->scratch->book2;
  decoder->book4 = decoder->scratch->book4;
  decoder->book8 = decoder->scratch->book8;
  decoder->silent = options != NULL && options->silent;
  decoder->frame_index = -1;
  (void)source;
  return true;
}

static void decoder_read_header(qa_roq_decoder *decoder, const char *source) {
  uint8_t stored[8];
  memcpy(stored, qa_roq_stream_initialize(decoder->stream), 8);
  qa_binary_reader reader;
  if (!qa_binary_reader_init(&reader, stored, 8, source)) {
    media_error("out of memory");
  }
  if (qa_roq_stream_initialized_by_reset(decoder->stream)) {
    qa_binary_reader_skip(&reader, 6);
  } else {
    if (qa_binary_reader_u16(&reader) != 0x1084) {
      qa_binary_error(source == NULL ? "<RoQ>" : source, 0,
                      "invalid RoQ magic");
    }
    qa_binary_reader_skip(&reader, 4);
  }
  int64_t rate = qa_binary_reader_u16(&reader);
  decoder->rate = rate == 0 ? 30 : rate;
  qa_binary_reader_destroy(&reader);
}

bool qa_roq_decoder_init_stream(qa_roq_decoder *decoder,
                                qa_roq_stream *stream, const char *source,
                                const qa_roq_decoder_options *options) {
  memset(decoder, 0, sizeof *decoder);
  if (!decoder_setup(decoder, source, options)) return false;
  /* TS: a RoqStream input always selects cinematic lookahead. */
  decoder->cinematic_lookahead = true;
  (void)options;
  decoder->stream = stream;
  decoder->owns_stream = false;
  decoder_read_header(decoder, source);
  return true;
}

bool qa_roq_decoder_init_bytes(qa_roq_decoder *decoder, const uint8_t *data,
                               size_t length, const char *source,
                               const qa_roq_decoder_options *options) {
  memset(decoder, 0, sizeof *decoder);
  if (!decoder_setup(decoder, source, options)) return false;
  qa_roq_end_policy policy =
      options != NULL && options->has_end_policy ? options->end_policy
                                                 : QA_ROQ_END_COMPLETE;
  decoder->cinematic_lookahead = policy == QA_ROQ_END_CINEMATIC_LOOKAHEAD;
  decoder->stream = qa_roq_stream_from_bytes(
      data, length, source == NULL ? "<RoQ>" : source, decoder->scratch->file,
      QA_ROQ_SCRATCH_FILE_LENGTH, policy);
  if (decoder->stream == NULL) {
    qa_roq_decoder_destroy(decoder);
    return false;
  }
  decoder->owns_stream = true;
  decoder_read_header(decoder, source);
  return true;
}

void qa_roq_decoder_destroy(qa_roq_decoder *decoder) {
  if (decoder->owns_stream) qa_roq_stream_destroy(decoder->stream);
  if (decoder->owns_scratch) qa_roq_scratch_destroy(&decoder->owned_scratch);
  memset(decoder, 0, sizeof *decoder);
}

int64_t qa_roq_decoder_frame_rate(const qa_roq_decoder *decoder) {
  return decoder->rate;
}
int64_t qa_roq_decoder_width(const qa_roq_decoder *decoder) {
  return decoder->frame_width;
}
int64_t qa_roq_decoder_height(const qa_roq_decoder *decoder) {
  return decoder->frame_height;
}
bool qa_roq_decoder_in_packet(const qa_roq_decoder *decoder) {
  return qa_roq_stream_in_packet(decoder->stream);
}
bool qa_roq_decoder_has_invalid_lookahead(const qa_roq_decoder *decoder) {
  return decoder->unknown_chunk != QA_ROQ_UNKNOWN_NONE ||
         qa_roq_stream_has_invalid_lookahead(decoder->stream);
}
bool qa_roq_decoder_reset_after_run(const qa_roq_decoder *decoder) {
  return decoder->unknown_chunk == QA_ROQ_UNKNOWN_RUN_TAIL;
}

void qa_roq_decoder_rewind(qa_roq_decoder *decoder) {
  qa_roq_stream_rewind(decoder->stream);
  uint8_t stored[8];
  memcpy(stored, qa_roq_stream_initialize(decoder->stream), 8);
  qa_binary_reader reader;
  if (!qa_binary_reader_init(&reader, stored, 8, decoder->stream->source)) {
    media_error("out of memory");
  }
  qa_binary_reader_skip(&reader, 6);
  int64_t rate = qa_binary_reader_u16(&reader);
  decoder->rate = rate == 0 ? 30 : rate;
  qa_binary_reader_destroy(&reader);
  decoder->frame_index = -1;
  decoder->unknown_chunk = QA_ROQ_UNKNOWN_NONE;
}

static uint8_t *front_of(qa_roq_decoder *decoder, size_t *length) {
  if ((decoder->frame_index & 1) == 0) {
    *length = decoder->first_length;
    return decoder->first_buffer;
  }
  *length = decoder->second_length;
  return decoder->second_buffer;
}
static uint8_t *back_of(qa_roq_decoder *decoder, size_t *length) {
  if ((decoder->frame_index & 1) == 0) {
    *length = decoder->second_length;
    return decoder->second_buffer;
  }
  *length = decoder->first_length;
  return decoder->first_buffer;
}

static noreturn void fail_at(qa_binary_reader *reader, const char *message) {
  qa_binary_error(reader->source, (int64_t)reader->cursor, "%s", message);
}

static void read_info(qa_roq_decoder *decoder, qa_binary_reader *reader) {
  if (decoder->frame_index != -1) {
    /* RoQInterrupt does not read repeated INFO payloads, and
     * retains numQuads=1. */
    if (decoder->frame_index != 1) decoder->frame_index = 0;
    return;
  }
  int64_t width = qa_binary_reader_u16(reader);
  int64_t height = qa_binary_reader_u16(reader);
  qa_binary_reader_u16(reader);
  qa_binary_reader_u16(reader);
  if (width == 0 || height == 0 || width % 8 != 0 || height % 8 != 0 ||
      width * height > 512 * 512) {
    fail_at(reader, "invalid RoQ quad dimensions");
  }
  decoder->frame_width = width;
  decoder->frame_height = height;
  int64_t byte_length = width * height * 4;
  decoder->first_buffer =
      qa_roq_scratch_frame(decoder->scratch, byte_length, 0);
  decoder->first_length = (size_t)byte_length;
  decoder->second_buffer =
      qa_roq_scratch_frame(decoder->scratch, byte_length, 1);
  decoder->second_length = (size_t)byte_length;
  decoder->frame_index = 0;
}

static bool read_audio(qa_roq_decoder *decoder, qa_binary_reader *reader,
                       int64_t flags, int channels, qa_roq_event *out) {
  size_t remaining = qa_binary_reader_remaining(reader);
  if (remaining % (size_t)channels != 0) {
    fail_at(reader, "incomplete stereo sample pair");
  }
  int64_t count = (int64_t)remaining;
  size_t allocated = channels == 1 ? (size_t)count * 2 : (size_t)count;
  int16_t *samples =
      (int16_t *)malloc((allocated == 0 ? 1 : allocated) * sizeof(int16_t));
  if (samples == NULL) return false;
  uint8_t *input = qa_binary_reader_bytes(reader, count);
  if (input == NULL && count != 0) {
    free(samples);
    return false;
  }
  if (channels == 1) {
    qa_source_roq_audio_decode_mono_to_stereo(
        &decoder->audio, input, (size_t)count, samples, allocated, count,
        false, flags);
  } else {
    qa_source_roq_audio_decode_stereo_to_stereo(
        &decoder->audio, input, (size_t)count, samples, allocated, count,
        false, flags);
  }
  free(input);
  /* RoQInterrupt submits RllDecodeMonoToStereo's duplicated buffer
   * as mono. */
  memset(out, 0, sizeof *out);
  out->kind = QA_ROQ_EVENT_AUDIO;
  out->samples = samples;
  out->sample_count = (size_t)count;
  out->channels = channels;
  out->sample_rate = 22050;
  return true;
}

static void blit(qa_roq_decoder *decoder, const uint8_t *book, int index,
                 int64_t size, int64_t x, int64_t y) {
  size_t front_length = 0;
  uint8_t *front = front_of(decoder, &front_length);
  (void)front_length;
  for (int64_t row = 0; row < size; row++) {
    size_t source = (size_t)((index * size * size + row * size) * 4);
    size_t target =
        (size_t)(((y + row) * decoder->frame_width + x) * 4);
    memcpy(front + target, book + source, (size_t)(size * 4));
  }
}

static void decode_block(qa_roq_decoder *decoder, vq_reader *vq, int64_t x,
                         int64_t y, int64_t size, int64_t flags);

static void motion_block(qa_roq_decoder *decoder, vq_reader *vq, int64_t x,
                         int64_t y, int64_t size, int64_t flags) {
  int motion = qa_binary_reader_u8(vq->reader);
  int32_t f = (int32_t)flags;
  int mean_x = (int32_t)((uint32_t)f << 16) >> 24;
  int mean_y = (int32_t)((uint32_t)f << 24) >> 24;
  int64_t scale = decoder->frame_width == decoder->frame_height * 4 ? 2 : 1;
  size_t front_length = 0;
  uint8_t *front = front_of(decoder, &front_length);
  int64_t reference =
      (decoder->frame_index & 1) == 0 ? (int64_t)front_length : 0;
  int64_t offset =
      reference +
      ((y + (8 - (motion & 15) - mean_y) * scale) * decoder->frame_width +
       x + (8 - (motion >> 4) - mean_x) * scale) * 4;
  int64_t length = ((size - 1) * decoder->frame_width + size) * 4;
  /* Source motion addresses the full linbuf, including the current
   * image and unused tail. */
  if (offset < 0 || offset + length > 512 * 512 * 4 * 2) {
    fail_at(vq->reader, "RoQ motion vector exceeds physical frame buffer");
  }
  uint8_t *source = qa_roq_scratch_view(decoder->scratch, offset, length);
  for (int64_t row = 0; row < size; row++) {
    for (int64_t pair = 0; pair < size * 4; pair += 8) {
      size_t from = (size_t)(row * decoder->frame_width * 4 + pair);
      size_t target =
          (size_t)(((y + row) * decoder->frame_width + x) * 4 + pair);
      memcpy(front + target, source + from, 8);
    }
  }
}

static void decode_block(qa_roq_decoder *decoder, vq_reader *vq, int64_t x,
                         int64_t y, int64_t size, int64_t flags) {
  switch (vq_code(vq)) {
    case 0:
      return;
    case 1:
      motion_block(decoder, vq, x, y, size, flags);
      return;
    case 2:
      blit(decoder, size == 8 ? decoder->book8 : decoder->book4,
           qa_binary_reader_u8(vq->reader), size, x, y);
      return;
    case 3:
      for (int quadrant = 0; quadrant < 4; quadrant++) {
        int64_t next_x = x + (quadrant & 1) * (size / 2);
        int64_t next_y = y + (quadrant >> 1) * (size / 2);
        if (size == 8) {
          decode_block(decoder, vq, next_x, next_y, 4, flags);
        } else {
          blit(decoder, decoder->book2, qa_binary_reader_u8(vq->reader), 2,
               next_x, next_y);
        }
      }
      return;
    default:
      return;
  }
}

static bool read_frame(qa_roq_decoder *decoder, qa_binary_reader *reader,
                       int64_t flags, qa_roq_event *out) {
  if (decoder->frame_width == 0) {
    fail_at(reader, "RoQ frame precedes quad info");
  }
  vq_reader vq;
  vq.reader = reader;
  vq.codes = 0;
  vq.remaining = 0;
  for (int64_t y = 0; y < decoder->frame_height; y += 16) {
    for (int64_t x = 0; x < decoder->frame_width; x += 16) {
      for (int quadrant = 0; quadrant < 4; quadrant++) {
        int64_t block_x = x + (quadrant & 1) * 8;
        int64_t block_y = y + (quadrant >> 1) * 8;
        if (block_x + 8 <= decoder->frame_width &&
            block_y + 8 <= decoder->frame_height) {
          decode_block(decoder, &vq, block_x, block_y, 8, flags);
        }
      }
    }
  }
  size_t front_length = 0;
  uint8_t *front = front_of(decoder, &front_length);
  uint8_t *rgba = (uint8_t *)malloc(front_length == 0 ? 1 : front_length);
  if (rgba == NULL) return false;
  memcpy(rgba, front, front_length);
  memset(out, 0, sizeof *out);
  out->kind = QA_ROQ_EVENT_FRAME;
  out->rgba = rgba;
  out->rgba_length = front_length;
  out->index = decoder->frame_index;
  out->time = (double)decoder->frame_index * 1000.0 / (double)decoder->rate;
  if (decoder->frame_index == 0) {
    size_t back_length = 0;
    uint8_t *back = back_of(decoder, &back_length);
    memcpy(back, front, back_length < front_length ? back_length
                                                   : front_length);
  }
  decoder->frame_index++;
  return true;
}

bool qa_roq_decoder_next_chunk(
    qa_roq_decoder *decoder,
    void (*before_stereo_decode)(void *ctx), void *before_ctx,
    void (*after_info_decode)(void *ctx), void *info_ctx,
    void (*after_audio_decode)(void *ctx, const qa_roq_event *audio),
    void *audio_ctx, qa_roq_event *out) {
  memset(out, 0, sizeof *out);
  if (decoder->unknown_chunk != QA_ROQ_UNKNOWN_NONE) {
    out->kind = QA_ROQ_EVENT_END;
    return true;
  }
  qa_roq_stream_chunk streamed;
  memset(&streamed, 0, sizeof streamed);
  if (!qa_roq_stream_next_chunk(decoder->stream, &streamed)) {
    out->kind = QA_ROQ_EVENT_END;
    return true;
  }
  qa_binary_reader *chunk = &streamed.reader;
  int64_t id = streamed.header.id;
  int64_t flags = streamed.header.flags;
  bool ok = true;
  switch (id) {
    case 0x1001:
      read_info(decoder, chunk);
      if (after_info_decode != NULL) after_info_decode(info_ctx);
      out->kind = QA_ROQ_EVENT_INFO;
      out->width = decoder->frame_width;
      out->height = decoder->frame_height;
      break;
    case 0x1002: {
      qa_roq_codebook_format format;
      format.samples_per_pixel = 4;
      format.gray = NULL;
      format.gray_length = 0;
      qa_source_roq_codebooks_decode(
          &decoder->scratch->codebooks, chunk, flags,
          QA_ROQ_CODEBOOK_NORMAL, format,
          decoder->cinematic_lookahead ? QA_ROQ_CODEBOOK_SOURCE
                                       : QA_ROQ_CODEBOOK_DIAGNOSTIC_2X2);
      out->kind = QA_ROQ_EVENT_METADATA;
      break;
    }
    case 0x1011:
      ok = read_frame(decoder, chunk, flags, out);
      break;
    case 0x1012: /* ROQ_QUAD_JPEG is a no-op in the original decoder. */
    case 0x1013:
    case 0x1030:
      out->kind = QA_ROQ_EVENT_METADATA;
      break;
    case 0x1020:
      if (decoder->silent) {
        out->kind = QA_ROQ_EVENT_METADATA;
      } else {
        ok = read_audio(decoder, chunk, flags, 1, out);
      }
      break;
    case 0x1021:
      if (decoder->silent) {
        out->kind = QA_ROQ_EVENT_METADATA;
      } else {
        if (before_stereo_decode != NULL) before_stereo_decode(before_ctx);
        ok = read_audio(decoder, chunk, flags, 2, out);
      }
      break;
    default:
      if (!decoder->cinematic_lookahead) {
        char message[64];
        snprintf(message, sizeof message, "unsupported RoQ chunk 0x%llx",
                 (long long)id);
        fail_at(chunk, message);
      }
      out->kind = QA_ROQ_EVENT_END;
      break;
  }
  if (!ok) {
    qa_binary_reader_destroy(chunk);
    return false;
  }
  if (out->kind == QA_ROQ_EVENT_AUDIO && after_audio_decode != NULL) {
    after_audio_decode(audio_ctx, out);
  }
  qa_binary_reader_destroy(chunk);
  qa_roq_stream_complete_chunk(decoder->stream,
                               out->kind == QA_ROQ_EVENT_END);
  if (out->kind == QA_ROQ_EVENT_END) {
    decoder->unknown_chunk =
        qa_roq_stream_has_invalid_lookahead(decoder->stream)
            ? QA_ROQ_UNKNOWN_INTERRUPT
            : QA_ROQ_UNKNOWN_RUN_TAIL;
  }
  return true;
}

bool qa_roq_decoder_next(qa_roq_decoder *decoder, qa_roq_event *out) {
  for (;;) {
    if (!qa_roq_decoder_next_chunk(decoder, NULL, NULL, NULL, NULL, NULL,
                                   NULL, out)) {
      return false;
    }
    if (out->kind != QA_ROQ_EVENT_INFO &&
        out->kind != QA_ROQ_EVENT_METADATA) {
      return true;
    }
    qa_roq_event_dispose(out);
  }
}

static const char *unknown_name(qa_roq_unknown_chunk unknown) {
  switch (unknown) {
    case QA_ROQ_UNKNOWN_INTERRUPT: return "interrupt";
    case QA_ROQ_UNKNOWN_RUN_TAIL: return "run-tail";
    case QA_ROQ_UNKNOWN_NONE: return "none";
  }
  return "none";
}

qa_save_value *qa_roq_decoder_capture_checkpoint(qa_roq_decoder *decoder) {
  qa_save_value *scratch = qa_roq_scratch_capture_checkpoint(decoder->scratch);
  qa_save_value *stream = qa_roq_stream_capture_checkpoint(decoder->stream);
  qa_save_value *object = qa_save_value_object();
  if (scratch == NULL || stream == NULL || object == NULL) {
    qa_save_value_free(scratch);
    qa_save_value_free(stream);
    qa_save_value_free(object);
    return NULL;
  }
  bool ok = set_number(object, "rate", (double)decoder->rate) &&
            set_number(object, "width", (double)decoder->frame_width) &&
            set_number(object, "height", (double)decoder->frame_height) &&
            set_number(object, "frameIndex", (double)decoder->frame_index) &&
            set_value(object, "unknownChunk",
                      qa_save_value_string(
                          unknown_name(decoder->unknown_chunk))) &&
            set_bool(object, "cinematicLookahead",
                     decoder->cinematic_lookahead) &&
            set_bool(object, "silent", decoder->silent) &&
            set_value(object, "scratch", scratch) &&
            set_value(object, "stream", stream);
  if (!ok) {
    qa_save_value_free(object);
    return NULL;
  }
  return object;
}

void qa_roq_decoder_restore_checkpoint(qa_roq_decoder *decoder,
                                       const qa_save_value *value) {
  qa_save_reader r;
  reader_init(&r, value, "roq-decoder");
  qa_save_reader f;
  read_field(&r, "cinematicLookahead", &f);
  qa_save_reader_literal_boolean(&f, decoder->cinematic_lookahead);
  qa_save_reader_destroy(&f);
  read_field(&r, "silent", &f);
  qa_save_reader_literal_boolean(&f, decoder->silent);
  qa_save_reader_destroy(&f);
  read_field(&r, "width", &f);
  int64_t width = (int64_t)qa_save_reader_integer_min(&f, 0);
  qa_save_reader_destroy(&f);
  read_field(&r, "height", &f);
  int64_t height = (int64_t)qa_save_reader_integer_min(&f, 0);
  qa_save_reader_destroy(&f);
  if ((width == 0) != (height == 0) || width % 8 != 0 || height % 8 != 0 ||
      width * height > 512 * 512) {
    qa_save_reader_fail(&r, "invalid frame dimensions");
  }
  read_field(&r, "rate", &f);
  decoder->rate = (int64_t)qa_save_reader_integer_min(&f, 1);
  qa_save_reader_destroy(&f);
  read_field(&r, "frameIndex", &f);
  decoder->frame_index = (int64_t)qa_save_reader_integer_min(&f, -1);
  qa_save_reader_destroy(&f);
  static const char *const unknown_choices[3] = {"none", "interrupt",
                                                 "run-tail"};
  read_field(&r, "unknownChunk", &f);
  const char *unknown = qa_save_reader_choice_string(&f, unknown_choices, 3);
  decoder->unknown_chunk = strcmp(unknown, "interrupt") == 0
                               ? QA_ROQ_UNKNOWN_INTERRUPT
                               : strcmp(unknown, "run-tail") == 0
                                     ? QA_ROQ_UNKNOWN_RUN_TAIL
                                     : QA_ROQ_UNKNOWN_NONE;
  qa_save_reader_destroy(&f);
  read_field(&r, "scratch", &f);
  qa_roq_scratch_restore_checkpoint(decoder->scratch, f.value);
  qa_save_reader_destroy(&f);
  read_field(&r, "stream", &f);
  qa_roq_stream_restore_checkpoint(decoder->stream, f.value);
  qa_save_reader_destroy(&f);
  decoder->frame_width = width;
  decoder->frame_height = height;
  if (width == 0) {
    decoder->first_buffer = decoder->scratch->frames;
    decoder->first_length = 0;
    decoder->second_buffer = decoder->scratch->frames;
    decoder->second_length = 0;
  } else {
    int64_t byte_length = width * height * 4;
    decoder->first_buffer =
        qa_roq_scratch_frame(decoder->scratch, byte_length, 0);
    decoder->first_length = (size_t)byte_length;
    decoder->second_buffer =
        qa_roq_scratch_frame(decoder->scratch, byte_length, 1);
    decoder->second_length = (size_t)byte_length;
  }
  qa_save_reader_destroy(&r);
}
