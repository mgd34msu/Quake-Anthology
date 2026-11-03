/* Port of src/platform/signal-hint.ts. GPL-2.0-or-later. */
#include "qa/platform/signal_hint.h"

#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

void qa_require_no_signal_handlers(int set_result) {
  const char *environment = getenv("SDL_NO_SIGNAL_HANDLERS");
  if (set_result == 1 || (environment != NULL && strcmp(environment, "1") == 0)) return;
  qa_raise("Error", 0, 0, "SDL must leave signal handling to the Unix signal owner");
}
