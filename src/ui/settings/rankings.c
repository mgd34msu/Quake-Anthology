/* Port of src/ui/settings/rankings.ts. GPL-2.0-or-later. */
#include "qa/ui/settings/rankings.h"

#include <string.h>

qa_ui_ranking_account_view qa_ui_ranking_account_view_for(
    const qa_ui_ranking_account_actions *actions) {
  qa_ranking_service_state service = actions->service(actions->ctx);
  qa_ui_ranking_account_view out;
  memset(&out, 0, sizeof out);
  if (strcmp(service.kind, "disabled") == 0) {
    out.kind = "disabled";
  } else if (strcmp(service.kind, "unavailable") == 0) {
    out.kind = "unavailable";
    out.message = service.reason;
  } else if (strcmp(service.kind, "starting") == 0
             || strcmp(service.kind, "ending") == 0) {
    out.kind = "busy";
  } else {
    out.kind = "account";
    out.player = actions->player(actions->ctx);
  }
  return out;
}
