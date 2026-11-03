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
noreturn void qa_app_q2_game_callback_error(const char *cause) {
  qa_raise("Q2GameCallbackError", 0, 0, "Q2 game callback failed: %s", cause);
}
/* TS q2GameCallback: try { callback() } catch (error) { throw error instanceof Q2GameCallbackError
 * ? error : new Q2GameCallbackError(error) } */
void qa_app_q2_game_callback(void (*fn)(void *ctx), void *ctx) {
  qa_caught_error caught;
  if (qa_try(fn, ctx, &caught)) return;
  if (strcmp(caught.name, "Q2GameCallbackError") == 0) qa_rethrow(&caught);
  qa_app_q2_game_callback_error(caught.message);
}
