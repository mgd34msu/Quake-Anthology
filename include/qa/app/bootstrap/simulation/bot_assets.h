/* Port of src/app/bootstrap/simulation/bot-assets.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_BOT_ASSETS_H
#define QA_APP_BOOTSTRAP_SIMULATION_BOT_ASSETS_H
#include <stdbool.h>
#include <stddef.h>
typedef struct qa_app_sim_application_bot_assets {
  const char *kind; /* "q3"|"rerelease" */
  const char *source; /* "q1-rerelease"|"q2-rerelease"|NULL */
  const void *files; const void *knowledge; const void *localization;
} qa_app_sim_application_bot_assets;
typedef struct qa_app_sim_bot_assets_loader {
  void *ctx;
  const void *(*load)(void *ctx, const void *content, const void *simulation); /* returns assets ptr cast */
} qa_app_sim_bot_assets_loader;
qa_app_sim_application_bot_assets qa_app_sim_load_application_bot_assets(qa_app_sim_bot_assets_loader loader,
    const void *content, const void *simulation);
#endif
