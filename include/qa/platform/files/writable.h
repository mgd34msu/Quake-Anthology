/*
 * Port of src/platform/files/writable.ts: unbuffered writable binary
 * files inside one explicitly scoped user-data directory, with
 * resumable checkpoints. The TypeScript print callback becomes fn+ctx
 * (NULL fn drops output, like the default no-op). Files are
 * caller-allocated; init/open return false only where the TypeScript
 * returns null or on OOM. GPL-2.0-or-later.
 */
#ifndef QA_PLATFORM_FILES_WRITABLE_H
#define QA_PLATFORM_FILES_WRITABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void (*qa_writable_print_fn)(const char *text, void *ctx);

typedef enum qa_writable_file_mode {
  QA_WRITABLE_WRITE,
  QA_WRITABLE_APPEND,
  QA_WRITABLE_APPEND_SYNC,
} qa_writable_file_mode;

typedef enum qa_writable_seek_origin {
  QA_WRITABLE_SEEK_CURRENT,
  QA_WRITABLE_SEEK_END,
  QA_WRITABLE_SEEK_SET,
} qa_writable_seek_origin;

typedef struct qa_writable_file_checkpoint {
  char *path; /* owned; dispose with the checkpoint */
  qa_writable_file_mode mode;
  bool mode_valid; /* false mirrors a checkpoint with a foreign mode string */
  int64_t position;
} qa_writable_file_checkpoint;

typedef struct qa_writable_binary_file {
  int descriptor;
  qa_writable_file_mode mode;
  qa_writable_print_fn print;
  void *print_ctx;
  char *path; /* owned */
  int64_t position;
  bool ended;
} qa_writable_binary_file;

typedef struct qa_user_file_store {
  char *root; /* owned */
} qa_user_file_store;

void qa_writable_file_checkpoint_dispose(qa_writable_file_checkpoint *checkpoint);

/* Checkpoint path is a fresh copy. False only on OOM. */
bool qa_writable_binary_file_capture_checkpoint(qa_writable_binary_file *file,
                                                qa_writable_file_checkpoint *out);
/* Bytes written: the full length, or 0 after one zero-progress retry. */
int64_t qa_writable_binary_file_write(qa_writable_binary_file *file,
                                      const uint8_t *bytes, size_t length);
/* 0 on success, -1 when the target position is not a safe nonnegative one. */
int qa_writable_binary_file_seek(qa_writable_binary_file *file, int64_t offset,
                                 qa_writable_seek_origin origin);
void qa_writable_binary_file_close(qa_writable_binary_file *file);

/* False only on OOM. */
bool qa_user_file_store_init(qa_user_file_store *store, const char *root);
void qa_user_file_store_destroy(qa_user_file_store *store);
/* Resumes a checkpoint; append modes may create the destination. */
bool qa_user_file_store_resume(qa_user_file_store *store,
                               const qa_writable_file_checkpoint *state,
                               qa_writable_print_fn print, void *print_ctx,
                               qa_writable_binary_file *out);
/* False when the platform refuses the open (TypeScript null) or on OOM. */
bool qa_user_file_store_open(qa_user_file_store *store, const char *name,
                             qa_writable_file_mode mode,
                             qa_writable_print_fn print, void *print_ctx,
                             qa_writable_binary_file *out);

#endif
