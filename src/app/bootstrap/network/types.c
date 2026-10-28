/* Port of src/app/bootstrap/network/types.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/types.h"
#include <string.h>
#include "qa/foundation/support.h"
const char *qa_app_network_phase_name(qa_app_network_phase phase) {
  switch (phase) {
    case QA_APP_PHASE_CHALLENGING: return "challenging";
    case QA_APP_PHASE_CONNECTING: return "connecting";
    case QA_APP_PHASE_LOADING: return "loading";
    case QA_APP_PHASE_ACTIVE: return "active";
    case QA_APP_PHASE_CLOSED: return "closed";
    case QA_APP_PHASE_REJECTED: return "rejected";
  }
  return "closed";
}
void qa_app_q2_game_callback(const char *(*fn)(void *ctx), void *ctx) {
  const char *error = fn(ctx);
  if (error == NULL) return;
  if (strncmp(error, "Q2 game callback failed: ", 25) == 0) qa_range_error("%s", error);
  qa_range_error("Q2 game callback failed: %s", error);
}
