/* Port of src/core/diagnostics.ts. GPL-2.0-or-later. */
#include "qa/core/diagnostics.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const kind_names[] = {"disconnect", "drop", "fatal"};

const char *qa_engine_error_kind_name(qa_engine_error_kind kind) {
  return kind_names[kind];
}

bool qa_engine_error_kind_parse(const char *text, qa_engine_error_kind *out) {
  for (size_t i = 0; i < sizeof kind_names / sizeof kind_names[0]; i++) {
    if (strcmp(text, kind_names[i]) == 0) {
      *out = (qa_engine_error_kind)i;
      return true;
    }
  }
  return false;
}

noreturn void qa_engine_error(qa_engine_error_kind kind, const char *message) {
  (void)kind; /* The kind travels on the TS error object; abort keeps none. */
  fprintf(stderr, "EngineError: %s\n", message);
  abort();
}

void qa_session_diagnostics_init(qa_session_diagnostics *diagnostics,
                                 qa_session_id session, qa_diagnostic_sink sink) {
  diagnostics->session = session;
  diagnostics->sink = sink;
}

void qa_session_diagnostics_emit(const qa_session_diagnostics *diagnostics,
                                 qa_diagnostic_input input) {
  qa_diagnostic diagnostic = {input.severity, input.code, input.message,
                              diagnostics->session, input.has_provider,
                              input.provider, input.has_time, input.time};
  diagnostics->sink.emit(diagnostics->sink.ctx, &diagnostic);
}
