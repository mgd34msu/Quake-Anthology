/* Port of src/app/bootstrap/network/q2-downloads.ts.
 * MountedContent/RemoteContentMounts (content + app siblings) are
 * supplied through the mounts/content vtables below. Async flows
 * are synchronous; the HTTP task runs inside prepare but its
 * completion is observed on the next prepare call, preserving the
 * TS waiting sequence. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_Q2_DOWNLOADS_H
#define QA_APP_BOOTSTRAP_NETWORK_Q2_DOWNLOADS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/network/client_download_policy.h"
#include "qa/app/bootstrap/network/types.h"
#include "qa/contracts/content.h"
#include "qa/core/cvars/index.h"
#include "qa/network/q2/server_messages.h"
#include "qa/network/services/downloads.h"
/* TS MountedContent subset used here. open fills malloc'd bytes +
 * the resolved reference; false is TS null. */
typedef struct qa_app_q2_mounts {
  void *ctx;
  bool (*open)(void *ctx, const char *path, uint8_t **out_bytes, size_t *out_length,
      const qa_resolved_resource_reference **out_reference);
  bool (*exists)(void *ctx, const char *path);
  void (*invalidate_listings)(void *ctx);
} qa_app_q2_mounts;
typedef struct qa_app_q2_downloads qa_app_q2_downloads;
qa_app_q2_downloads *qa_app_create_q2_application_downloads(
    qa_app_q2_mounts mounts, qa_cvar_registry *cvars, bool edition_rerelease);
void qa_app_q2_downloads_destroy(qa_app_q2_downloads *downloads);
/* Malloc'd URL text or NULL (TS null). */
char *qa_app_q2_downloads_http_server(qa_app_q2_downloads *downloads);
bool qa_app_q2_downloads_allowed(qa_app_q2_downloads *downloads, const char *name);
/* false is TS null; source ctx owned by the returned source. */
bool qa_app_q2_downloads_open(qa_app_q2_downloads *downloads, const char *name,
    qa_download_source *out);
typedef struct qa_app_q2_peer_download qa_app_q2_peer_download;
qa_app_q2_peer_download *qa_app_q2_peer_download_create(void);
void qa_app_q2_peer_download_destroy(qa_app_q2_peer_download *peer);
double qa_app_q2_peer_download_revision(const qa_app_q2_peer_download *peer);
/* Returned event is malloc'd; free with qa_q2_server_event_clear +
 * free. NULL is TS null. offset_text NULL is TS undefined. */
qa_q2_server_event *qa_app_q2_peer_download_begin(qa_app_q2_peer_download *peer,
    qa_app_q2_downloads *downloads, const char *name, const char *offset_text);
qa_q2_server_event *qa_app_q2_peer_download_next(qa_app_q2_peer_download *peer);
void qa_app_q2_peer_download_close(qa_app_q2_peer_download *peer);
typedef enum qa_app_q2_download_preparation {
  QA_APP_Q2_PREP_READY, QA_APP_Q2_PREP_WAITING, QA_APP_Q2_PREP_CANCELED
} qa_app_q2_download_preparation;
typedef enum qa_app_q2_download_receive {
  QA_APP_Q2_DL_COMPLETE, QA_APP_Q2_DL_WAITING, QA_APP_Q2_DL_UNSOLICITED
} qa_app_q2_download_receive;
/* TS RemoteContentMounts fields read by the receiver, plus the
 * scoped-open used by the HTTP resolved() check. */
typedef struct qa_app_q2_remote_mounts {
  qa_app_q2_mounts mounts;
  const char *selection_base, *selection_directory;
  const char *write_root, *base_write_root;
  const char *corpus_root, *product_content_directory, *product_loose_root;
  /* mounts.open with the TS game-local root filter already applied
   * by the caller-side content owner. */
  bool (*open_scoped)(void *ctx, const char *path, bool game_local);
} qa_app_q2_remote_mounts;
/* Accessors over Q2ApplicationGameState's data/configStrings. */
typedef struct qa_app_q2_state_access {
  const char *(*gamedir)(const void *data);
  const char *(*config_string)(const void *config_strings, double index);
} qa_app_q2_state_access;
typedef struct qa_app_q2_download_receiver qa_app_q2_download_receiver;
/* permission NULL means the TS default allow-all. refresh_packages
 * NULL is TS undefined. */
qa_app_q2_download_receiver *qa_app_q2_download_receiver_create(
    const qa_app_q2_remote_mounts *(*content)(void *ctx), void *content_ctx,
    void (*command)(void *ctx, const char *text), void *command_ctx,
    void (*print)(void *ctx, const char *text), void *print_ctx,
    void (*refresh_packages)(void *ctx), void *refresh_ctx,
    const qa_app_client_download_permission *permission,
    qa_app_q2_state_access state_access);
void qa_app_q2_download_receiver_destroy(qa_app_q2_download_receiver *receiver);
size_t qa_app_q2_download_receiver_progress(qa_app_q2_download_receiver *receiver,
    qa_app_client_download_progress *out, size_t capacity);
void qa_app_q2_download_receiver_cancel(qa_app_q2_download_receiver *receiver);
void qa_app_q2_download_receiver_retry(qa_app_q2_download_receiver *receiver);
double qa_app_q2_download_receiver_revision(const qa_app_q2_download_receiver *receiver);
void qa_app_q2_download_receiver_set_http_server(qa_app_q2_download_receiver *receiver,
    const char *server_url);
qa_app_q2_download_preparation qa_app_q2_download_receiver_prepare(
    qa_app_q2_download_receiver *receiver, const qa_app_q2_game_state *state);
qa_app_q2_download_receive qa_app_q2_download_receiver_receive(
    qa_app_q2_download_receiver *receiver, const qa_q2_server_event *block);
void qa_app_q2_download_receiver_close(qa_app_q2_download_receiver *receiver);
#endif
