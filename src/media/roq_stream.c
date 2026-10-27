/* Port of src/media/roq-stream.ts. GPL-2.0-or-later. */
#include "qa/media/roq_stream.h"

#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* TS plain `throw new Error(msg)`. */
static noreturn void media_error(const char *format, ...) {
  va_list args;
  va_start(args, format);
  fputs("Error: ", stderr);
  vfprintf(stderr, format, args);
  fputc('\n', stderr);
  va_end(args);
  abort();
}

const char *qa_roq_end_policy_name(qa_roq_end_policy policy) {
  return policy == QA_ROQ_END_CINEMATIC_LOOKAHEAD ? "cinematic-lookahead"
                                                  : "complete";
}

static char *dup_text(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = (char *)malloc(length);
  if (copy != NULL) memcpy(copy, text, length);
  return copy;
}

static qa_roq_stream *stream_new(const char *source, uint8_t *buffer,
                                 size_t buffer_length) {
  qa_roq_stream *stream = (qa_roq_stream *)calloc(1, sizeof *stream);
  if (stream == NULL) return NULL;
  stream->source = dup_text(source);
  if (stream->source == NULL) {
    free(stream);
    return NULL;
  }
  stream->buffer = buffer;
  stream->buffer_length = buffer_length;
  stream->played = 24;
  return stream;
}

qa_roq_stream *qa_roq_stream_open(qa_media_open_fn open_fn, void *open_ctx,
                                  const char *source, uint8_t *buffer,
                                  size_t buffer_length) {
  qa_media_input *input = open_fn(open_ctx);
  if (input == NULL) return NULL;
  if (input->byte_length == 0) {
    qa_media_input_close(input);
    return NULL;
  }
  qa_roq_stream *stream = stream_new(source, buffer, buffer_length);
  if (stream == NULL) {
    qa_media_input_close(input);
    return NULL;
  }
  stream->file_kind = QA_ROQ_FILE_OPEN;
  stream->open_fn = open_fn;
  stream->open_ctx = open_ctx;
  stream->input = input;
  stream->length = input->byte_length;
  stream->end_policy = QA_ROQ_END_CINEMATIC_LOOKAHEAD;
  return stream;
}

qa_roq_stream *qa_roq_stream_from_bytes(const uint8_t *data, size_t length,
                                        const char *source, uint8_t *buffer,
                                        size_t buffer_length,
                                        qa_roq_end_policy end_policy) {
  qa_roq_stream *stream = stream_new(source, buffer, buffer_length);
  if (stream == NULL) return NULL;
  stream->file_kind = QA_ROQ_FILE_BYTES;
  stream->data = data;
  stream->data_length = length;
  stream->length = (int64_t)length;
  stream->end_policy = end_policy;
  return stream;
}

void qa_roq_stream_destroy(qa_roq_stream *stream) {
  if (stream == NULL) return;
  qa_roq_stream_close(stream);
  free(stream->source);
  free(stream);
}

static int64_t stream_read(qa_roq_stream *stream, int64_t length) {
  if (stream->file_kind == QA_ROQ_FILE_CLOSED) {
    media_error("RoQ file is closed");
  }
  if (length > (int64_t)stream->buffer_length) {
    qa_binary_error(stream->source, stream->position,
                    "RoQ chunk exceeds source scratch buffer");
  }
  int64_t count;
  if (stream->file_kind == QA_ROQ_FILE_BYTES) {
    int64_t available = (int64_t)stream->data_length - stream->position;
    if (available < 0) available = 0;
    count = length < available ? length : available;
    if (count > 0) {
      memcpy(stream->buffer, stream->data + (size_t)stream->position,
             (size_t)count);
    }
  } else if (stream->file_kind == QA_ROQ_FILE_MISSING) {
    count = 0;
  } else {
    count = qa_media_input_read_at(stream->input, stream->position,
                                   stream->buffer, (size_t)length);
  }
  stream->position += count;
  return count;
}

static qa_roq_chunk_header parse_header(qa_roq_stream *stream,
                                        int64_t offset) {
  char source[1024];
  snprintf(source, sizeof source, "%s@%lld", stream->source,
           (long long)(stream->buffer_offset + offset));
  qa_binary_reader reader;
  if (!qa_binary_reader_init(&reader, stream->buffer + (size_t)offset, 8,
                             source)) {
    media_error("out of memory");
  }
  qa_roq_chunk_header header;
  header.id = qa_binary_reader_u16(&reader);
  /* cl_cin reads three size bytes and skips the fourth. */
  header.size = (int64_t)(qa_binary_reader_u32(&reader) & 0xffffffu);
  header.flags = qa_binary_reader_u16(&reader);
  qa_binary_reader_destroy(&reader);
  return header;
}

const uint8_t *qa_roq_stream_initialize(qa_roq_stream *stream) {
  if (stream->has_header) return stream->header_bytes;
  int64_t count = stream_read(stream, 16);
  if (count != 16 &&
      !(stream->file_kind == QA_ROQ_FILE_BYTES && count == 8 &&
        stream->length == 8)) {
    qa_binary_error(stream->source, stream->position,
                    "truncated RoQ header");
  }
  stream->buffer_offset = stream->position - count;
  stream->buffered_length = count;
  memcpy(stream->header_bytes, stream->buffer, 8);
  stream->has_header = true;
  if (count == 8) {
    stream->has_next_header = false;
  } else {
    stream->next_header = parse_header(stream, 8);
    stream->has_next_header = true;
  }
  if (stream->has_next_header && stream->next_header.size > 65536) {
    qa_binary_error(stream->source, 10, "RoQ chunk exceeds 65536 bytes");
  }
  return stream->header_bytes;
}

void qa_roq_stream_begin_playback(qa_roq_stream *stream) {
  stream->has_header = false;
  stream->reset_header = false;
  stream->played = 24;
  stream->invalid_lookahead = false;
  stream->retained_eof = false;
  stream->buffered_next = false;
  stream->has_pending = false;
  qa_roq_stream_initialize(stream);
}

bool qa_roq_stream_initialized_by_reset(const qa_roq_stream *stream) {
  return stream->reset_header;
}

bool qa_roq_stream_has_invalid_lookahead(const qa_roq_stream *stream) {
  return stream->invalid_lookahead;
}

bool qa_roq_stream_in_packet(const qa_roq_stream *stream) {
  return stream->buffered_next;
}

static int64_t scratch_limit(const qa_roq_stream *stream, bool buffered) {
  if (!buffered) return stream->buffered_length;
  int64_t length = (int64_t)stream->buffer_length;
  return length < 65536 ? length : 65536;
}

bool qa_roq_stream_next_chunk(qa_roq_stream *stream,
                              qa_roq_stream_chunk *out) {
  if (stream->has_pending) {
    media_error("RoQ chunk dispatch has not completed");
  }
  if (stream->invalid_lookahead) return false;
  if (!stream->has_header) {
    media_error("RoQ stream has not been initialized");
  }
  if (!stream->has_next_header) return false;
  qa_roq_chunk_header header = stream->next_header;
  if (!stream->buffered_next) {
    int64_t offset = stream->position;
    int64_t count = stream_read(stream, header.size + 8);
    /* RoQInterrupt performs this read even when RoQPlayed already
     * names EOF. */
    if (stream->end_policy == QA_ROQ_END_CINEMATIC_LOOKAHEAD &&
        stream->played >= stream->length) {
      return false;
    }
    if (stream->end_policy == QA_ROQ_END_COMPLETE) {
      if (count < header.size) {
        qa_binary_error(stream->source, offset + count,
                        "truncated RoQ payload");
      }
      stream->buffered_length = count;
    } else {
      if (count < header.size + 8) {
        if (count == header.size && stream->position == stream->length) {
          stream->retained_eof = true;
        } else if (count != 0 || !stream->retained_eof) {
          qa_binary_error(stream->source, offset + count,
                          "truncated RoQ payload or lookahead header");
        }
      }
      if (count == header.size + 8 && stream->position == stream->length) {
        stream->retained_eof = true;
      }
      stream->buffered_length = header.size + 8;
    }
    stream->buffer_offset = offset;
    stream->chunk_offset = 0;
  }
  int64_t offset = stream->chunk_offset;
  int64_t decoded_size =
      header.id == 0x1030 || header.id == 0x1013 ? 0 : header.size;
  int64_t limit = scratch_limit(stream, stream->buffered_next);
  if (offset + decoded_size > limit) {
    qa_binary_error(stream->source, stream->buffer_offset + offset,
                    "RoQ packet payload exceeds source scratch buffer");
  }
  stream->has_pending = true;
  stream->pending_header = header;
  stream->pending_offset = offset;
  stream->pending_buffered = stream->buffered_next;
  /* decodeCodeBook follows its entry counts beyond the chunk into
   * retained cin.file bytes. */
  int64_t end = header.id == 0x1002 &&
                        stream->end_policy == QA_ROQ_END_CINEMATIC_LOOKAHEAD
                    ? scratch_limit(stream, true)
                    : offset + decoded_size;
  char source[1024];
  snprintf(source, sizeof source, "%s@%lld", stream->source,
           (long long)(stream->buffer_offset + offset));
  out->header = header;
  if (!qa_binary_reader_init(&out->reader,
                             stream->buffer + (size_t)offset,
                             (size_t)(end - offset), source)) {
    media_error("out of memory");
  }
  return true;
}

void qa_roq_stream_complete_chunk(qa_roq_stream *stream, bool ended) {
  if (!stream->has_pending) {
    media_error("RoQ has no pending chunk dispatch");
  }
  qa_roq_chunk_header header = stream->pending_header;
  int64_t offset = stream->pending_offset;
  bool buffered = stream->pending_buffered;
  if (header.id == 0x1030) stream->in_memory = header.flags;
  int64_t following = offset + (header.id == 0x1030 || header.id == 0x1013
                                    ? 0
                                    : header.size);
  if (stream->end_policy == QA_ROQ_END_COMPLETE &&
      following == stream->buffered_length && stream->in_memory == 0) {
    stream->has_next_header = false;
    stream->buffered_next = false;
    stream->has_pending = false;
    return;
  }
  int64_t limit = scratch_limit(stream, buffered);
  if (following + 8 > limit) {
    qa_binary_error(stream->source, stream->buffer_offset + following,
                    "truncated RoQ packet or lookahead header");
  }
  stream->next_header = parse_header(stream, following);
  stream->has_next_header = true;
  stream->invalid_lookahead =
      stream->next_header.size > 65536 || stream->next_header.id == 0x1084;
  stream->has_pending = false;
  stream->buffered_next = false;
  if (stream->invalid_lookahead) return;
  if (stream->in_memory != 0 && !ended) {
    stream->in_memory--;
    stream->chunk_offset = following + 8;
    stream->buffered_next = true;
  } else {
    stream->played += stream->next_header.size + 8;
  }
}

void qa_roq_stream_rewind(qa_roq_stream *stream) {
  qa_roq_stream_file_kind kind = stream->file_kind;
  if (kind == QA_ROQ_FILE_CLOSED) {
    media_error("Cannot reset a closed RoQ file");
  }
  if (kind != QA_ROQ_FILE_BYTES) {
    qa_media_open_fn open_fn = stream->open_fn;
    void *open_ctx = stream->open_ctx;
    qa_roq_stream_close(stream);
    qa_media_input *input = open_fn(open_ctx);
    stream->open_fn = open_fn;
    stream->open_ctx = open_ctx;
    if (input == NULL) {
      stream->file_kind = QA_ROQ_FILE_MISSING;
    } else {
      stream->file_kind = QA_ROQ_FILE_OPEN;
      stream->input = input;
    }
  }
  stream->position = 0;
  stream->played = 24;
  stream->invalid_lookahead = false;
  stream->retained_eof = false;
  stream->buffered_next = false;
  stream->has_pending = false;
  /* RoQReset ignores the read count, including FS_Read(handle=0),
   * and does not recheck the magic, size word or first chunk size
   * before RoQ_init. */
  int64_t count = stream_read(stream, 16);
  stream->buffer_offset = 0;
  stream->buffered_length = 16;
  memcpy(stream->header_bytes, stream->buffer, 8);
  stream->has_header = true;
  if (kind == QA_ROQ_FILE_BYTES && count == 8 && stream->length == 8) {
    stream->has_next_header = false;
  } else {
    stream->next_header = parse_header(stream, 8);
    stream->has_next_header = true;
  }
  stream->reset_header = true;
}

void qa_roq_stream_close(qa_roq_stream *stream) {
  if (stream->file_kind == QA_ROQ_FILE_CLOSED) return;
  if (stream->file_kind == QA_ROQ_FILE_OPEN && stream->input != NULL) {
    qa_media_input_close(stream->input);
    stream->input = NULL;
  }
  stream->file_kind = QA_ROQ_FILE_CLOSED;
}

void qa_roq_stream_retire(qa_roq_stream *stream) {
  /* Common owns final table closure; the input is dropped without
   * calling into its ended lifetime, as in the TS. */
  stream->input = NULL;
  stream->file_kind = QA_ROQ_FILE_CLOSED;
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

qa_save_value *qa_roq_stream_capture_checkpoint(qa_roq_stream *stream) {
  if (stream->has_pending) {
    media_error("Cannot checkpoint a pending RoQ chunk dispatch");
  }
  qa_save_value *object = qa_save_value_object();
  if (object == NULL) return NULL;
  qa_save_value *next_header = NULL;
  bool ok = true;
  if (stream->has_next_header) {
    next_header = qa_save_value_object();
    ok = next_header != NULL &&
         set_number(next_header, "id", (double)stream->next_header.id) &&
         set_number(next_header, "size", (double)stream->next_header.size) &&
         set_number(next_header, "flags", (double)stream->next_header.flags);
  } else {
    next_header = qa_save_value_null();
    ok = next_header != NULL;
  }
  qa_save_value *header = stream->has_header
                              ? qa_save_value_bytes(stream->header_bytes, 8)
                              : qa_save_value_null();
  ok = ok && header != NULL &&
       set_number(object, "length", (double)stream->length) &&
       set_value(object, "endPolicy",
                 qa_save_value_string(
                     qa_roq_end_policy_name(stream->end_policy))) &&
       set_bool(object, "closed",
                stream->file_kind == QA_ROQ_FILE_CLOSED) &&
       set_bool(object, "missing",
                stream->file_kind == QA_ROQ_FILE_MISSING) &&
       set_number(object, "position", (double)stream->position) &&
       set_number(object, "played", (double)stream->played) &&
       set_value(object, "header", header) &&
       set_value(object, "nextHeader", next_header) &&
       set_bool(object, "invalidLookahead", stream->invalid_lookahead) &&
       set_bool(object, "retainedEof", stream->retained_eof) &&
       set_bool(object, "resetHeader", stream->reset_header) &&
       set_number(object, "inMemory", (double)stream->in_memory) &&
       set_bool(object, "bufferedNext", stream->buffered_next) &&
       set_number(object, "chunkOffset", (double)stream->chunk_offset) &&
       set_number(object, "bufferOffset", (double)stream->buffer_offset) &&
       set_number(object, "bufferedLength",
                  (double)stream->buffered_length) &&
       set_value(object, "buffer",
                 qa_save_value_bytes(stream->buffer, stream->buffer_length));
  if (!ok) {
    qa_save_value_free(object);
    return NULL;
  }
  return object;
}

static bool read_field(const qa_save_reader *reader, const char *name,
                       qa_save_reader *out) {
  if (qa_save_reader_field(reader, name, out)) return true;
  media_error("out of memory");
}

void qa_roq_stream_restore_checkpoint(qa_roq_stream *stream,
                                      const qa_save_value *value) {
  if (stream->has_pending) {
    media_error("Cannot restore a pending RoQ chunk dispatch");
  }
  qa_save_reader r;
  if (!qa_save_reader_init(&r, value, "roq-stream")) {
    media_error("out of memory");
  }
  qa_save_reader f;
  read_field(&r, "length", &f);
  qa_save_reader_literal_number(&f, (double)stream->length);
  qa_save_reader_destroy(&f);
  read_field(&r, "endPolicy", &f);
  qa_save_reader_literal_string(&f,
                                qa_roq_end_policy_name(stream->end_policy));
  qa_save_reader_destroy(&f);
  read_field(&r, "buffer", &f);
  size_t buffer_length = 0;
  const uint8_t *buffer = qa_save_reader_bytes(&f, &buffer_length);
  if (buffer_length != stream->buffer_length) {
    qa_save_reader_fail(&f, "scratch buffer size differs");
  }
  uint8_t *buffer_copy = (uint8_t *)malloc(buffer_length == 0 ? 1 : buffer_length);
  if (buffer_copy == NULL) media_error("out of memory");
  memcpy(buffer_copy, buffer, buffer_length);
  qa_save_reader_destroy(&f);
  bool has_header = false;
  uint8_t header_bytes[8] = {0};
  read_field(&r, "header", &f);
  if (!qa_save_reader_is_null(&f)) {
    size_t header_length = 0;
    const uint8_t *header = qa_save_reader_bytes(&f, &header_length);
    if (header_length != 8) {
      qa_save_reader_fail(&r, "invalid retained header");
    }
    memcpy(header_bytes, header, 8);
    has_header = true;
  }
  qa_save_reader_destroy(&f);
  bool has_next = false;
  qa_roq_chunk_header next_header = {0, 0, 0};
  read_field(&r, "nextHeader", &f);
  if (!qa_save_reader_is_null(&f)) {
    qa_save_reader g;
    read_field(&f, "id", &g);
    next_header.id = (int64_t)qa_save_reader_integer_min(&g, 0);
    qa_save_reader_destroy(&g);
    read_field(&f, "size", &g);
    next_header.size = (int64_t)qa_save_reader_integer_min(&g, 0);
    qa_save_reader_destroy(&g);
    read_field(&f, "flags", &g);
    next_header.flags = (int64_t)qa_save_reader_integer_min(&g, 0);
    qa_save_reader_destroy(&g);
    has_next = true;
  }
  qa_save_reader_destroy(&f);
  read_field(&r, "position", &f);
  int64_t position = (int64_t)qa_save_reader_integer_min(&f, 0);
  qa_save_reader_destroy(&f);
  read_field(&r, "bufferedLength", &f);
  int64_t buffered_length = (int64_t)qa_save_reader_integer_min(&f, 0);
  qa_save_reader_destroy(&f);
  if (position > stream->length ||
      buffered_length > (int64_t)stream->buffer_length) {
    qa_save_reader_fail(&r, "stream cursor outside input");
  }
  read_field(&r, "closed", &f);
  bool closed = qa_save_reader_boolean(&f);
  qa_save_reader_destroy(&f);
  read_field(&r, "missing", &f);
  bool missing = qa_save_reader_boolean(&f);
  qa_save_reader_destroy(&f);
  if (missing && stream->file_kind != QA_ROQ_FILE_MISSING) {
    qa_save_reader_fail(&r, "missing source is no longer missing");
  }
  read_field(&r, "played", &f);
  stream->played = (int64_t)qa_save_reader_integer_min(&f, 0);
  qa_save_reader_destroy(&f);
  read_field(&r, "inMemory", &f);
  stream->in_memory = (int64_t)qa_save_reader_integer_min(&f, 0);
  qa_save_reader_destroy(&f);
  read_field(&r, "chunkOffset", &f);
  stream->chunk_offset = (int64_t)qa_save_reader_integer_min(&f, 0);
  qa_save_reader_destroy(&f);
  read_field(&r, "bufferOffset", &f);
  stream->buffer_offset = (int64_t)qa_save_reader_integer_min(&f, 0);
  qa_save_reader_destroy(&f);
  read_field(&r, "invalidLookahead", &f);
  stream->invalid_lookahead = qa_save_reader_boolean(&f);
  qa_save_reader_destroy(&f);
  read_field(&r, "retainedEof", &f);
  stream->retained_eof = qa_save_reader_boolean(&f);
  qa_save_reader_destroy(&f);
  read_field(&r, "resetHeader", &f);
  stream->reset_header = qa_save_reader_boolean(&f);
  qa_save_reader_destroy(&f);
  read_field(&r, "bufferedNext", &f);
  stream->buffered_next = qa_save_reader_boolean(&f);
  qa_save_reader_destroy(&f);
  stream->position = position;
  stream->buffered_length = buffered_length;
  stream->has_header = has_header;
  memcpy(stream->header_bytes, header_bytes, 8);
  stream->has_next_header = has_next;
  stream->next_header = next_header;
  memcpy(stream->buffer, buffer_copy, buffer_length);
  free(buffer_copy);
  qa_save_reader_destroy(&r);
  if (closed) qa_roq_stream_close(stream);
}
