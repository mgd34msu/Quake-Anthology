/* Port of src/app/bootstrap/network/q3-types.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/q3_types.h"
#include <string.h>
#include "qa/foundation/support.h"
void qa_app_q3_game_callback(const char *(*fn)(void *ctx), void *ctx) {
  const char *error = fn(ctx);
  if (error == NULL) return;
  qa_raise(QA_APP_Q3_GAME_CALLBACK_ERROR, 0, 0, "Q3 game callback failed: %s", error);
}
/* TS q3GameCallback: `catch (error) { throw error instanceof Q3GameCallbackError
 * ? error : new Q3GameCallbackError(error) }`. */
void qa_app_q3_game_call(void (*fn)(void *ctx), void *ctx) {
  qa_caught_error caught;
  if (qa_try(fn, ctx, &caught)) return;
  if (strcmp(caught.name, QA_APP_Q3_GAME_CALLBACK_ERROR) == 0) qa_rethrow(&caught);
  qa_raise(QA_APP_Q3_GAME_CALLBACK_ERROR, 0, 0, "Q3 game callback failed: %s", caught.message);
}
