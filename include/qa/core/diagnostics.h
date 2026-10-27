/*
 * Port of src/core/diagnostics.ts: the Diagnostic record, EngineError,
 * and SessionDiagnostics. C accommodations: the TS `| null` fields
 * become has_* flags; the sink closure becomes function pointer + ctx;
 * throwing EngineError becomes qa_engine_error(), which prints
 * "EngineError: <message>" and aborts (the class sets name =
 * "EngineError"; the kind travels on the TS error object and aborts
 * keep none, as with CommonError).
 * GPL-2.0-or-later.
 */
#ifndef QA_CORE_DIAGNOSTICS_H
#define QA_CORE_DIAGNOSTICS_H

#include <stdbool.h>
#include <stdnoreturn.h>

#include "qa/contracts/identity.h"
#include "qa/contracts/time.h"

/* TS Diagnostic.severity. */
typedef enum qa_diagnostic_severity {
  QA_DIAGNOSTIC_DEBUG,   /* "debug" */
  QA_DIAGNOSTIC_INFO,    /* "info" */
  QA_DIAGNOSTIC_WARNING, /* "warning" */
  QA_DIAGNOSTIC_ERROR    /* "error" */
} qa_diagnostic_severity;

typedef struct qa_diagnostic {
  qa_diagnostic_severity severity;
  const char *code;
  const char *message;
  qa_session_id session;
  bool has_provider;
  qa_provider_id provider; /* valid when has_provider */
  bool has_time;
  qa_source_time time; /* valid when has_time */
} qa_diagnostic;

/** TS Omit<Diagnostic, "session">: what SessionDiagnostics.emit takes. */
typedef struct qa_diagnostic_input {
  qa_diagnostic_severity severity;
  const char *code;
  const char *message;
  bool has_provider;
  qa_provider_id provider;
  bool has_time;
  qa_source_time time;
} qa_diagnostic_input;

/** TS DiagnosticSink closure. */
typedef struct qa_diagnostic_sink {
  void *ctx;
  void (*emit)(void *ctx, const qa_diagnostic *diagnostic);
} qa_diagnostic_sink;

/* TS EngineError kind. */
typedef enum qa_engine_error_kind {
  QA_ENGINE_ERROR_DISCONNECT, /* "disconnect" */
  QA_ENGINE_ERROR_DROP,       /* "drop" */
  QA_ENGINE_ERROR_FATAL       /* "fatal" */
} qa_engine_error_kind;

const char *qa_engine_error_kind_name(qa_engine_error_kind kind);
bool qa_engine_error_kind_parse(const char *text, qa_engine_error_kind *out);
noreturn void qa_engine_error(qa_engine_error_kind kind, const char *message);

/** TS SessionDiagnostics: stamps its session onto every emission. */
typedef struct qa_session_diagnostics {
  qa_session_id session;
  qa_diagnostic_sink sink;
} qa_session_diagnostics;

void qa_session_diagnostics_init(qa_session_diagnostics *diagnostics,
                                 qa_session_id session, qa_diagnostic_sink sink);
void qa_session_diagnostics_emit(const qa_session_diagnostics *diagnostics,
                                 qa_diagnostic_input input);

#endif
