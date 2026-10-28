/* Port of src/app/bootstrap/network/q3-client-downloads.ts.
 * DownloadMounts mirrors the unlanded app content selection.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_Q3_CLIENT_DOWNLOADS_H
#define QA_APP_BOOTSTRAP_NETWORK_Q3_CLIENT_DOWNLOADS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/network/client_download_policy.h"
#include "qa/network/q3/download.h"
#include "qa/network/q3/pak_references.h"
typedef struct qa_app_q3_download_mounts {
  const char *selection_base;      /* "q3-baseq3" */
  const char *selection_directory; /* selected game directory */
  const char *write_root, *base_write_root;
} qa_app_q3_download_mounts;
/* Malloc'd mapped path (may be a copy of `path`). NULL only on OOM. */
char *qa_app_q3_download_path(const char *path, const qa_app_q3_download_mounts *owner);
typedef struct qa_app_q3_download_bindings {
  void *ctx;
  const qa_app_client_download_permission *permission; /* may be NULL */
  void (*assert_current)(void *ctx);
  void (*reliable)(void *ctx, const char *text);
  void (*send_packet)(void *ctx);
  void (*progress)(void *ctx, const char *name, double count, double size);
  void (*reload_packages)(void *ctx);
} qa_app_q3_download_bindings;
typedef struct qa_app_q3_client_downloads qa_app_q3_client_downloads;
/* owner may be NULL. NULL return only on OOM. */
qa_app_q3_client_downloads *qa_app_q3_client_downloads_create(const char *root,
    qa_app_q3_download_bindings bindings, const qa_app_q3_download_mounts *owner);
void qa_app_q3_client_downloads_destroy(qa_app_q3_client_downloads *downloads);
/* Writes up to capacity rows; returns total row count. */
size_t qa_app_q3_client_downloads_progress(qa_app_q3_client_downloads *downloads,
    qa_app_client_download_progress *out, size_t capacity);
void qa_app_q3_client_downloads_cancel(qa_app_q3_client_downloads *downloads);
bool qa_app_q3_client_downloads_retry(qa_app_q3_client_downloads *downloads);
bool qa_app_q3_client_downloads_begin(qa_app_q3_client_downloads *downloads,
    const qa_q3_server_pak *referenced, size_t referenced_count,
    const double *loaded_checksums, size_t loaded_count,
    bool (*exists)(void *ctx, const char *path), void *exists_ctx);
double qa_app_q3_client_downloads_publish_size(qa_app_q3_client_downloads *downloads, double size);
void qa_app_q3_client_downloads_receive(qa_app_q3_client_downloads *downloads,
    const qa_q3_download_block *download);
void qa_app_q3_client_downloads_close(qa_app_q3_client_downloads *downloads);
#endif
