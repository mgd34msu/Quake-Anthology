/* Port of src/media/transitions.ts. GPL-2.0-or-later. */
#include "qa/media/transitions.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

qa_cinematic_transition_binding qa_cinematic_transition_make(
    qa_seat_id seat, qa_cinematic_transition transition,
    qa_cinematic_transition_host host) {
  qa_cinematic_transition_binding binding;
  binding.seat = seat;
  binding.transition = transition;
  binding.host = host;
  binding.completed = false;
  return binding;
}

void qa_cinematic_transition_fire(qa_cinematic_transition_binding *binding,
                                  qa_cinematic_end_reason reason) {
  if (binding->completed) return;
  binding->completed = true;
  qa_cinematic_transition_host *host = &binding->host;
  host->leave_cinematic(host->ctx, binding->seat);
  if (reason == QA_CINEMATIC_END_STOPPED) return;
  switch (binding->transition.kind) {
    case QA_CINEMATIC_TRANSITION_Q2_NEXTSERVER: {
      double count = binding->transition.server_count;
      if (!isfinite(count) || trunc(count) != count) {
        qa_range_error("Q2 server count must be an integer");
      }
      char command[64];
      if (count >= -9007199254740991.0 && count <= 9007199254740991.0) {
        snprintf(command, sizeof command, "nextserver %lld\n",
                 (long long)count);
      } else {
        snprintf(command, sizeof command, "nextserver %.17g\n", count);
      }
      host->send_client_command(host->ctx, binding->seat, command);
      return;
    }
    case QA_CINEMATIC_TRANSITION_Q3_NEXTMAP: {
      const char *text = binding->transition.command;
      if (text != NULL && text[0] != '\0') {
        size_t length = strlen(text);
        char *command = (char *)malloc(length + 2);
        if (command == NULL) qa_range_error("out of memory");
        memcpy(command, text, length);
        command[length] = '\n';
        command[length + 1] = '\0';
        host->append_command(host->ctx, binding->seat, command);
        free(command);
      }
      return;
    }
    case QA_CINEMATIC_TRANSITION_RETURN:
      return;
  }
}
