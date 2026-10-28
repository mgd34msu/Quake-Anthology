/* Port of src/app/bootstrap/network/q3-types.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/q3_types.h"
#include <string.h>
#include "qa/foundation/support.h"
void qa_app_q3_game_callback(const char *(*fn)(void *ctx), void *ctx) {
  const char *error = fn(ctx);
  if (error == NULL) return;
  if (strncmp(error, "Q3 game callback failed: ", 25) == 0) qa_range_error("%s", error);
  qa_range_error("Q3 game callback failed: %s", error);
}
