/* Port of src/network/q3/download.ts. GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q3_DOWNLOAD_H
#define QA_NETWORK_Q3_DOWNLOAD_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/network/q3/message.h"
#include "qa/network/q3/server_message.h"
/* TS Q3DownloadReadFile as fn+ctx. read fills target (2048 cap), returns bytes. */
typedef int64_t (*qa_q3_download_read_fn)(void *ctx, uint8_t *target, size_t capacity);
typedef void (*qa_q3_download_close_fn)(void *ctx);
typedef struct qa_q3_download_read_file {
  int64_t size;
  qa_q3_download_read_fn read; qa_q3_download_close_fn close; void *ctx;
} qa_q3_download_read_file;
typedef bool (*qa_q3_download_open_fn)(void *ctx, const char *name, qa_q3_download_read_file *out);
typedef bool (*qa_q3_download_bool_fn)(void *ctx);
typedef void (*qa_q3_download_drop_fn)(void *ctx, const char *reason);
/* TS Q3DownloadServerBindings. */
typedef struct qa_q3_download_server_bindings {
  qa_q3_download_open_fn open;
  qa_q3_download_bool_fn enabled, pure;
  qa_q3_download_drop_fn drop;
  qa_q3_message_print_fn print; void *print_ctx;
  void *ctx;
} qa_q3_download_server_bindings;
/* TS Q3DownloadRate (numbers stay double; rate math follows TS). */
typedef struct qa_q3_download_rate { double rate, max_rate, snapshot_msec; } qa_q3_download_rate;
typedef struct qa_q3_server_download qa_q3_server_download;
bool qa_q3_server_download_init(qa_q3_server_download **out, const qa_q3_download_server_bindings *bindings);
void qa_q3_server_download_destroy(qa_q3_server_download *d);
const char *qa_q3_server_download_name(const qa_q3_server_download *d);
void qa_q3_server_download_set_name(qa_q3_server_download *d, const char *name);
const qa_q3_download_server_bindings *qa_q3_server_download_get_bindings(const qa_q3_server_download *d);
void qa_q3_server_download_begin(qa_q3_server_download *d, const char *name);
void qa_q3_server_download_close(qa_q3_server_download *d);
void qa_q3_server_download_acknowledge(qa_q3_server_download *d, int64_t block, double time);
void qa_q3_server_download_write(qa_q3_server_download *d, qa_q3_message_writer *writer, double time, const qa_q3_download_rate *settings);
/* TS Q3DownloadWriteFile as fn+ctx. */
typedef void (*qa_q3_download_write_bytes_fn)(void *ctx, const uint8_t *bytes, size_t len);
typedef struct qa_q3_download_write_file {
  qa_q3_download_write_bytes_fn write_bytes; qa_q3_download_close_fn close; void *ctx;
} qa_q3_download_write_file;
typedef void (*qa_q3_download_thunk_fn)(void *ctx);
typedef bool (*qa_q3_download_open_temp_fn)(void *ctx, const char *path, qa_q3_download_write_file *out);
typedef void (*qa_q3_download_publish_temp_fn)(void *ctx, const char *temporary, const char *destination);
typedef void (*qa_q3_download_text_fn)(void *ctx, const char *text);
typedef void (*qa_q3_download_progress_fn)(void *ctx, const char *name, int64_t count, int64_t size);
/* TS Q3DownloadClientBindings. */
typedef struct qa_q3_download_client_bindings {
  qa_q3_download_thunk_fn assert_current;
  qa_q3_download_open_temp_fn open_temporary;
  qa_q3_download_publish_temp_fn publish_temporary;
  qa_q3_download_text_fn reliable;
  qa_q3_download_thunk_fn send_packet;
  qa_q3_download_progress_fn progress;
  qa_q3_download_thunk_fn completed;
  void *ctx;
} qa_q3_download_client_bindings;
typedef struct qa_q3_client_download qa_q3_client_download;
bool qa_q3_client_download_init(qa_q3_client_download **out, const qa_q3_download_client_bindings *bindings);
void qa_q3_client_download_destroy(qa_q3_client_download *d);
const qa_q3_download_client_bindings *qa_q3_client_download_get_bindings(const qa_q3_client_download *d);
void qa_q3_client_download_begin(qa_q3_client_download *d, const char *remote, const char *local);
int64_t qa_q3_client_download_publish_size(qa_q3_client_download *d, int64_t size);
void qa_q3_client_download_receive(qa_q3_client_download *d, const qa_q3_download_block *download);
void qa_q3_client_download_close(qa_q3_client_download *d);
#endif
