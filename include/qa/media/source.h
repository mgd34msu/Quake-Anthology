/*
 * Port of src/media/source.ts: the MediaInput random-access byte
 * source (in-memory bytes or one retained file descriptor).
 * C accommodations: inputs are heap allocated by the constructors;
 * qa_media_input_close closes and frees. mediaBytes borrows the
 * caller's bytes, as the TS closure views the same Uint8Array.
 * A custom input supplies read_at/close function pointers plus ctx
 * (TS object literal). GPL-2.0-or-later.
 */
#ifndef QA_MEDIA_SOURCE_H
#define QA_MEDIA_SOURCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct qa_media_input qa_media_input;
struct qa_media_input {
  void *ctx;
  char *source;      /* owned */
  int64_t byte_length;
  /* Returns bytes read into destination (TS readAt). */
  int64_t (*read_at)(void *ctx, int64_t offset, uint8_t *destination,
                     size_t length);
  void (*close_ctx)(void *ctx); /* nullable */
  bool closed;
};

/* TS mediaBytes; `source` NULL means "<media>". NULL only on OOM. */
qa_media_input *qa_media_bytes(const uint8_t *bytes, size_t length,
                               const char *source);
/* TS openMediaFile; NULL only on OOM (OS errors abort as thrown). */
qa_media_input *qa_open_media_file(const char *path);
/* Heap input over caller callbacks; copies `source`. NULL on OOM. */
qa_media_input *qa_media_input_create(void *ctx, const char *source,
                                      int64_t byte_length,
                                      int64_t (*read_at)(void *, int64_t,
                                                         uint8_t *, size_t),
                                      void (*close_ctx)(void *));
/* TS readAt, including the closed-input and offset checks. */
int64_t qa_media_input_read_at(qa_media_input *input, int64_t offset,
                               uint8_t *destination, size_t length);
/* Closes (idempotent in effect) and frees the input. */
void qa_media_input_close(qa_media_input *input);

/* TS readMedia: malloc'd exact-length read; caller frees. Length 0
 * may return NULL. NULL only on OOM; short input aborts as thrown. */
uint8_t *qa_read_media(qa_media_input *input, int64_t offset, int64_t length);

#endif
