/* Port of src/app/bootstrap/network/client-download-policy.ts.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_CLIENT_DOWNLOAD_POLICY_H
#define QA_APP_BOOTSTRAP_NETWORK_CLIENT_DOWNLOAD_POLICY_H
#include <stdbool.h>
#include "qa/core/cvars/index.h"
typedef enum qa_app_client_download_category {
  QA_APP_DL_METADATA, QA_APP_DL_PACKAGE, QA_APP_DL_MAP, QA_APP_DL_MODEL,
  QA_APP_DL_SOUND, QA_APP_DL_PLAYER, QA_APP_DL_PICTURE
} qa_app_client_download_category;
const char *qa_app_client_download_category_name(qa_app_client_download_category category);
qa_app_client_download_category qa_app_client_download_category_for(const char *path);
typedef enum qa_app_download_transport { QA_APP_DL_HTTP, QA_APP_DL_NATIVE } qa_app_download_transport;
/* TS ClientDownloadPermission closure over a borrowed registry. */
typedef struct qa_app_client_download_permission qa_app_client_download_permission;
qa_app_client_download_permission *qa_app_create_client_download_permission(
    qa_cvar_registry *cvars, bool family_q3);
bool qa_app_client_download_permitted(const qa_app_client_download_permission *permission,
    qa_app_download_transport transport, qa_app_client_download_category category);
void qa_app_client_download_permission_destroy(qa_app_client_download_permission *permission);
typedef enum qa_app_download_phase { QA_APP_DL_PENDING, QA_APP_DL_RUNNING, QA_APP_DL_DONE } qa_app_download_phase;
typedef struct qa_app_client_download_progress {
  const char *path; qa_app_download_transport transport;
  double received; bool has_total; double total; bool has_percent; double percent;
  qa_app_download_phase phase;
} qa_app_client_download_progress;
#endif
