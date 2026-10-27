/*
 * Port of src/media/roq-stream.ts (CIN_PlayCinematic, RoQInterrupt
 * and RoQReset framing from id Software cl_cin.c). One unique source
 * file, with only the current payload and following header in the
 * borrowed cin.file scratch buffer. C accommodations: streams are
 * heap allocated; close marks the file closed (rewind then aborts,
 * as in the TS), destroy closes and frees. The open factory is a
 * function pointer plus ctx returning a fresh input or NULL (TS
 * null). GPL-2.0-or-later.
 */
#ifndef QA_MEDIA_ROQ_STREAM_H
#define QA_MEDIA_ROQ_STREAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/core/binary/index.h"
#include "qa/media/source.h"
#include "qa/persistence/value.h"

typedef qa_media_input *(*qa_media_open_fn)(void *ctx);

typedef enum qa_roq_end_policy {
  QA_ROQ_END_COMPLETE,
  QA_ROQ_END_CINEMATIC_LOOKAHEAD
} qa_roq_end_policy;
const char *qa_roq_end_policy_name(qa_roq_end_policy policy);

typedef struct qa_roq_chunk_header {
  int64_t id;
  int64_t size;
  int64_t flags;
} qa_roq_chunk_header;

typedef struct qa_roq_stream_chunk {
  qa_roq_chunk_header header;
  qa_binary_reader reader; /* destroy with qa_binary_reader_destroy */
} qa_roq_stream_chunk;

typedef enum qa_roq_stream_file_kind {
  QA_ROQ_FILE_OPEN,
  QA_ROQ_FILE_MISSING,
  QA_ROQ_FILE_BYTES,
  QA_ROQ_FILE_CLOSED
} qa_roq_stream_file_kind;

typedef struct qa_roq_stream {
  char *source; /* owned */
  qa_roq_stream_file_kind file_kind;
  qa_media_open_fn open_fn; /* open/missing kinds */
  void *open_ctx;
  qa_media_input *input;      /* owned; open kind */
  const uint8_t *data;        /* borrowed; bytes kind */
  size_t data_length;
  int64_t length;
  uint8_t *buffer; /* borrowed cin.file scratch */
  size_t buffer_length;
  qa_roq_end_policy end_policy;
  int64_t position;
  int64_t played;
  uint8_t header_bytes[8];
  bool has_header;
  qa_roq_chunk_header next_header;
  bool has_next_header;
  bool invalid_lookahead;
  bool retained_eof;
  bool reset_header;
  int64_t in_memory;
  bool buffered_next;
  int64_t chunk_offset;
  int64_t buffer_offset;
  int64_t buffered_length;
  bool has_pending;
  qa_roq_chunk_header pending_header;
  int64_t pending_offset;
  bool pending_buffered;
} qa_roq_stream;

/* TS RoqStream.open: NULL when the factory yields null or an empty
 * input (and only on OOM). Takes ownership of the opened input. */
qa_roq_stream *qa_roq_stream_open(qa_media_open_fn open_fn, void *open_ctx,
                                  const char *source, uint8_t *buffer,
                                  size_t buffer_length);
/* TS RoqStream.fromBytes (borrows `data`). NULL only on OOM. */
qa_roq_stream *qa_roq_stream_from_bytes(const uint8_t *data, size_t length,
                                        const char *source, uint8_t *buffer,
                                        size_t buffer_length,
                                        qa_roq_end_policy end_policy);
void qa_roq_stream_destroy(qa_roq_stream *stream);

/* Borrowed 8-byte stream header (TS initialize result). */
const uint8_t *qa_roq_stream_initialize(qa_roq_stream *stream);
void qa_roq_stream_begin_playback(qa_roq_stream *stream);
bool qa_roq_stream_initialized_by_reset(const qa_roq_stream *stream);
bool qa_roq_stream_has_invalid_lookahead(const qa_roq_stream *stream);
bool qa_roq_stream_in_packet(const qa_roq_stream *stream);
/* False at end of stream (TS null); on true destroy the reader,
 * then qa_roq_stream_complete_chunk. */
bool qa_roq_stream_next_chunk(qa_roq_stream *stream,
                              qa_roq_stream_chunk *out);
void qa_roq_stream_complete_chunk(qa_roq_stream *stream, bool ended);
void qa_roq_stream_rewind(qa_roq_stream *stream);
void qa_roq_stream_close(qa_roq_stream *stream);
void qa_roq_stream_retire(qa_roq_stream *stream);

qa_save_value *qa_roq_stream_capture_checkpoint(qa_roq_stream *stream);
void qa_roq_stream_restore_checkpoint(qa_roq_stream *stream,
                                      const qa_save_value *value);

#endif
