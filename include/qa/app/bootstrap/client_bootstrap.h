/* Port of src/app/bootstrap/client-bootstrap.ts. This module is the
 * client composition interface set; concrete owners are assembled by
 * startup/application. localClientAccount's WeakMap becomes an
 * explicit registry here. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_CLIENT_BOOTSTRAP_H
#define QA_APP_BOOTSTRAP_CLIENT_BOOTSTRAP_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/common.h"
#include "qa/contracts/identity.h"
/* TS Account (network/services identity subset read here). */
typedef struct qa_app_local_account { char *id, *name; } qa_app_local_account;
/* TS ClientSourcePublicationError message. */
extern const char qa_app_client_source_publication_error[];
/* TS localClientAccount: stable per identity owner; id bytes come
 * from the host random source (16 bytes -> hex). */
typedef struct qa_app_local_accounts qa_app_local_accounts;
qa_app_local_accounts *qa_app_local_accounts_create(void (*random_bytes)(void *ctx, unsigned char out[16]), void *random_ctx);
void qa_app_local_accounts_destroy(qa_app_local_accounts *accounts);
/* Borrowed account for the owner (created on first use). */
const qa_app_local_account *qa_app_local_client_account(qa_app_local_accounts *accounts, const qa_identity_owner *identity);
/* TS ClientBootstrap / ClientSourceLifetime surfaces as fn+ctx rows
 * (implemented by Application in application.h). */
typedef struct qa_app_client_recording_feed {
  void *ctx;
  const char *root;
  const char *(*seed)(void *ctx, const void **out_seed);
  const void *(*attach)(void *ctx, const void *sink);
  void (*detach)(void *ctx, const void *token);
} qa_app_client_recording_feed;
typedef struct qa_app_client_source_lifetime {
  void *ctx;
  const char *(*prepare_recording)(void *ctx, qa_command_context source, qa_app_client_recording_feed *out);
  const char *(*prepare_video_restart)(void *ctx, const void **out_prepared);
  const char *capture_map; /* NULL mirrors TS null */
  const char *(*prepare_retirement)(void *ctx);
  void (*release_settings)(void *ctx);
  const char *(*retire)(void *ctx);
} qa_app_client_source_lifetime;
/* OS-random registry (TS node:crypto randomBytes(16)); NULL on OOM. */
qa_app_local_accounts *qa_app_local_accounts_create_os_random(void);
/* ClientSourceLifetime forwarding (TS `source?.method()` sites): a
 * NULL lifetime or NULL fn is a no-op returning NULL (no error), so
 * shrunk lifetime rows stay callable. Borrowed error text. */
const char *qa_app_client_source_prepare_recording(const qa_app_client_source_lifetime *lifetime,
    qa_command_context source, qa_app_client_recording_feed *out);
const char *qa_app_client_source_prepare_video_restart(const qa_app_client_source_lifetime *lifetime,
    const void **out_prepared);
const char *qa_app_client_source_prepare_retirement(const qa_app_client_source_lifetime *lifetime);
void qa_app_client_source_release_settings(const qa_app_client_source_lifetime *lifetime);
const char *qa_app_client_source_retire(const qa_app_client_source_lifetime *lifetime);
/* TS capture map fallback: lifetime->capture_map ?? "menu". */
const char *qa_app_client_source_capture_map(const qa_app_client_source_lifetime *lifetime);
#endif
