/* Port of src/app/bootstrap/simulation/bot-assets.ts. TS async load is
 * synchronous here (PORTING.md); mounted-file walking stays with the
 * content/bot asset loaders behind the callback. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/bot_assets.h"
#include <string.h>
#include "qa/foundation/support.h"
qa_app_sim_application_bot_assets qa_app_sim_load_application_bot_assets(qa_app_sim_bot_assets_loader loader,
    const void *content, const void *simulation) {
  const qa_app_sim_application_bot_assets *result = loader.load != NULL
      ? (const qa_app_sim_application_bot_assets *)loader.load(loader.ctx, content, simulation) : NULL;
  if (result == NULL) qa_range_error("Native bot definitions disappeared during loading");
  return *result;
}
