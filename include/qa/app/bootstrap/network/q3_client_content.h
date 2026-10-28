/* Port of src/app/bootstrap/network/q3-client-content.ts.
 * ApplicationOptions/LoadedApplicationContent (sibling app slices)
 * are supplied through the host; systeminfo parsing, product/game
 * validation, policy selection and pure admission stay here.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_Q3_CLIENT_CONTENT_H
#define QA_APP_BOOTSTRAP_NETWORK_Q3_CLIENT_CONTENT_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/network/q3_downloads.h"
#include "qa/content/mounts.h"
typedef struct qa_app_q3_content_host {
  void *ctx;
  /* catalog.require(options.product).expectation, resolved by caller. */
  const char *product_family;
  const char *product_content_directory;
  /* Mounts of the catalog content (first packages open). */
  const qa_app_q3_package_mount *catalog_mounts; size_t catalog_mount_count;
  /* loadApplicationContent: policy NULL when TS undefined. Returns
   * an opaque loaded-content handle; its mounts feed the second
   * packages open and collect() enumerates opened references. */
  void *(*load_content)(void *ctx, const qa_pure_mount_policy *policy);
  const qa_app_q3_package_mount *(*loaded_mounts)(void *ctx, void *loaded, size_t *out_count);
  void (*collect_opened)(void *ctx, void *loaded,
      void (*visit)(void *visit_ctx, const qa_resolved_resource_reference *reference, bool is_q3),
      void *visit_ctx);
  void (*close_content)(void *ctx, void *loaded);
} qa_app_q3_content_host;
typedef struct qa_app_q3_client_content qa_app_q3_client_content;
/* NULL only on OOM. On second-open failure the loaded content is
 * closed first (TS catch). */
qa_app_q3_client_content *qa_app_q3_client_content_open(qa_app_q3_content_host host,
    const char *info, double checksum_feed);
void qa_app_q3_client_content_destroy(qa_app_q3_client_content *content);
void *qa_app_q3_client_content_loaded(const qa_app_q3_client_content *content);
qa_app_q3_packages *qa_app_q3_client_content_packages(qa_app_q3_client_content *content);
bool qa_app_q3_client_content_has_policy(const qa_app_q3_client_content *content);
bool qa_app_q3_client_content_pure(const qa_app_q3_client_content *content);
bool qa_app_q3_client_content_matches(qa_app_q3_client_content *content,
    const char *info, double checksum_feed);
/* Malloc'd referenced-pure command text. */
char *qa_app_q3_client_content_referenced_pure_command(
    qa_app_q3_client_content *content, double server_id);
void qa_app_q3_client_content_close(qa_app_q3_client_content *content);
#endif
