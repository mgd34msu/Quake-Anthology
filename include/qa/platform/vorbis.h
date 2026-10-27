/*
 * Port of src/platform/vorbis.ts: libvorbisfile decoding separated
 * from track/game ownership (Q1/Q2 cd_ogg). Linked directly instead
 * of through Bun FFI. VorbisError throws become a false return plus
 * an optional error record; argument RangeErrors keep aborting with
 * the TypeScript message. Chunk samples are malloc'd and survive
 * decoder reads, seeks and close. GPL-2.0-or-later.
 */
#ifndef QA_PLATFORM_VORBIS_H
#define QA_PLATFORM_VORBIS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <vorbis/vorbisfile.h>

typedef struct qa_vorbis_error {
  char operation[32];
  int code; /* Vorbis error code; 0 for plain errors */
  char message[160]; /* "<op> failed with Vorbis error <code>" or the plain text */
} qa_vorbis_error;

typedef struct qa_vorbis_metadata {
  int64_t sample_rate;
  int channels; /* 1 or 2 */
  int64_t total_frames;
  double duration_seconds;
  int64_t logical_streams;
} qa_vorbis_metadata;

typedef struct qa_vorbis_pcm_chunk {
  int16_t *samples; /* owned */
  size_t sample_count;
  int64_t frames;
  int64_t sample_rate;
  int channels; /* 1 or 2 */
} qa_vorbis_pcm_chunk;

typedef struct qa_vorbis_decoder {
  OggVorbis_File file;
  bool opened;
  qa_vorbis_metadata metadata;
} qa_vorbis_decoder;

/* The TypeScript qualifies only the 64-bit LP64 ABI; direct linkage
 * inherits the build ABI, so open performs no platform gate. */
bool qa_vorbis_decoder_open(qa_vorbis_decoder *decoder, const char *path,
                            qa_vorbis_error *error);
bool qa_vorbis_decoder_closed(const qa_vorbis_decoder *decoder);
bool qa_vorbis_decoder_position_frames(qa_vorbis_decoder *decoder, int64_t *out,
                                       qa_vorbis_error *error);
bool qa_vorbis_decoder_seek(qa_vorbis_decoder *decoder, int64_t frame,
                            qa_vorbis_error *error);
/* On success *out_has_chunk is false only at EOF (TypeScript null). */
bool qa_vorbis_decoder_read(qa_vorbis_decoder *decoder, int64_t max_frames,
                            qa_vorbis_pcm_chunk *out, bool *out_has_chunk,
                            qa_vorbis_error *error);
/* Returns the ov_clear code (0 on success); idempotent. */
int qa_vorbis_decoder_close(qa_vorbis_decoder *decoder, qa_vorbis_error *error);

void qa_vorbis_pcm_chunk_dispose(qa_vorbis_pcm_chunk *chunk);

#endif
