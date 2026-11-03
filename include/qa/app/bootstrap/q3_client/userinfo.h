/* Port of src/app/bootstrap/q3-client/userinfo.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q3_CLIENT_USERINFO_H
#define QA_APP_BOOTSTRAP_Q3_CLIENT_USERINFO_H
#include <stdbool.h>
#include "qa/core/cvars/index.h"
/* TS identity argument of initializeQ3ClientCvars. */
typedef struct qa_app_q3_client_identity {
  const char *name;
  const char *model;
} qa_app_q3_client_identity;
/* TS q3ClientModel: returns model (borrowed) or "sarge". */
const char *qa_app_q3_client_model(const char *character, const char *model);
/* TS applyQ3ProxyModel. */
void qa_app_apply_q3_proxy_model(qa_cvar_registry *cvars, const char *character);
/* TS initializeQ3ClientCvars. */
void qa_app_initialize_q3_client_cvars(qa_cvar_registry *cvars, qa_app_q3_client_identity identity);
/* TS applyServerSystemInfo(cvars, info, skip): CL_SystemInfoChanged. */
void qa_app_apply_server_system_info(qa_cvar_registry *cvars, const char *info,
    bool (*skip)(void *ctx, const char *name), void *skip_ctx);
#endif
