/* Port of src/bots/behavior/library/log.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_LIBRARY_LOG_H
#define QA_BOTS_BEHAVIOR_LIBRARY_LOG_H
#include "qa/bots/behavior/library/libvars.h"
#include "qa/persistence/value.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef enum qa_bot_log_io_kind { QA_LOG_IO_OK, QA_LOG_IO_FAILED } qa_bot_log_io_kind;
typedef struct qa_bot_log_io_result {
  qa_bot_log_io_kind kind;
} qa_bot_log_io_result;
typedef struct qa_bot_log_stream_bots_a {
  void *ctx;
  qa_bot_log_io_result (*write)(void *ctx, const uint8_t *bytes, size_t n);
  qa_bot_log_io_result (*flush)(void *ctx);
  qa_bot_log_io_result (*close)(void *ctx);
  bool has_checkpoint;
  int64_t (*checkpoint_position)(void *ctx);
} qa_bot_log_stream_bots_a;
typedef struct qa_bot_log_open_result {
  bool opened;
  qa_bot_log_stream_bots_a stream;
} qa_bot_log_open_result;
typedef struct qa_bot_log {
  qa_bot_lib_vars *variables;
  double time;
  void (*print)(void *ctx, int severity, const char *text);
  void *print_ctx;
  qa_bot_log_open_result (*open_file)(void *ctx, const char *name);
  void *open_ctx;
  /* TS resumeFile: opens an existing log without writes or truncation, retaining
   * the saved position. NULL is the absent member. */
  qa_bot_log_open_result (*resume_file)(void *ctx, const char *name, int64_t position);
  void *resume_ctx;
  bool is_open;
  bool retained_closed;
  qa_bot_log_stream_bots_a stream;
  char filename[1025];
  char *opened_filename;
  int32_t numwrites;
} qa_bot_log;
void qa_bot_log_init(qa_bot_log *l, qa_bot_lib_vars *vars);
void qa_bot_log_destroy(qa_bot_log *l);
qa_bot_log_io_result qa_bot_log_open(qa_bot_log *l, const char *filename);
qa_bot_log_io_result qa_bot_log_close(qa_bot_log *l);
qa_bot_log_io_result qa_bot_log_shutdown(qa_bot_log *l);
qa_bot_log_io_result qa_bot_log_dispose_resources(qa_bot_log *l);
void qa_bot_log_write(qa_bot_log *l, const char *formatted);
void qa_bot_log_write_time_stamped(qa_bot_log *l, const char *formatted);
void qa_bot_log_flush(qa_bot_log *l);
bool qa_bot_log_has_file(const qa_bot_log *l);
int64_t qa_bot_log_file_write(qa_bot_log *l, const char *formatted);
/* TS captureSaveState: owned tree. Aborts with the TS message when the active
 * stream cannot checkpoint, leaving it open. */
qa_save_value *qa_bot_log_capture_save_state(qa_bot_log *l);
/* TS restoreSaveState: requires an empty owner; never writes or truncates. */
void qa_bot_log_restore_save_state(qa_bot_log *l, const qa_save_value *value);
#endif
