/* Port of src/app/bootstrap/network/qw-downloads.ts. Async ->
 * synchronous; the landed sink reports write failures by aborting
 * where the TS catch prints and returns 'missing' (noted in .c).
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_QW_DOWNLOADS_H
#define QA_APP_BOOTSTRAP_NETWORK_QW_DOWNLOADS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/network/client_download_policy.h"
#include "qa/app/bootstrap/network/qw_types.h"
typedef struct qa_app_qw_download_options {
  void *ctx;
  const char *game_root, *skin_root;
  bool (*exists)(void *ctx, const char *path, qa_app_qw_download_category category);
  void (*send_command)(void *ctx, const char *text);
  void (*print)(void *ctx, const char *text);
  double (*noskins)(void *ctx);
  bool (*demo_recording)(void *ctx);
  bool (*demo_playback)(void *ctx);
  bool has_maximum_bytes; double maximum_bytes;
  const qa_app_client_download_permission *permission; /* borrowed, may be NULL */
} qa_app_qw_download_options;
typedef struct qa_app_qw_download_result {
  bool missing; double percent; const uint8_t *bytes; size_t length;
} qa_app_qw_download_result;
typedef struct qa_app_qw_download_receiver qa_app_qw_download_receiver;
qa_app_qw_download_receiver *qa_app_qw_download_receiver_create(qa_app_qw_download_options options);
void qa_app_qw_download_receiver_destroy(qa_app_qw_download_receiver *receiver);
void qa_app_qw_download_receiver_cancel(qa_app_qw_download_receiver *receiver);
qa_app_qw_download_request_result qa_app_qw_download_receiver_retry(qa_app_qw_download_receiver *receiver);
/* Returns 0 or 1 progress rows (storage owned by the receiver). */
size_t qa_app_qw_download_receiver_progress(qa_app_qw_download_receiver *receiver,
    qa_app_client_download_progress *out);
qa_app_qw_download_request_result qa_app_qw_download_receiver_request(
    qa_app_qw_download_receiver *receiver, const char *path, qa_app_qw_download_category category);
qa_app_qw_download_receive_result qa_app_qw_download_receiver_receive(
    qa_app_qw_download_receiver *receiver, qa_app_qw_download_result result);
void qa_app_qw_download_receiver_close(qa_app_qw_download_receiver *receiver);
#endif
