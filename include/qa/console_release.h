#ifndef QA_CONSOLE_RELEASE_H
#define QA_CONSOLE_RELEASE_H
#include "qa/console.h"

typedef struct qa_console_release qa_console_release;
typedef enum qa_console_release_outcome {
    QA_CONSOLE_RELEASE_UNENTERED,
    QA_CONSOLE_RELEASE_WAITING,
    QA_CONSOLE_RELEASE_COMPLETED,
    QA_CONSOLE_RELEASE_FAILED
} qa_console_release_outcome;
/* Retains an actual captured context and preallocated release program while
 * borrowing the same console's registered handlers and lifetime. */
bool qa_console_release_prepare(qa_console *, const qa_command_context *, const char *,
    qa_console_release **, qa_error *);
/* The entered program uses the actual console queue, including aliases,
 * scripts and waits. The original pending program remains retained. Failed
 * commands keep the normal consumed-command outcome and are never replayed. */
bool qa_console_release_advance(qa_console_release *, qa_console_release_outcome *, qa_error *);
bool qa_console_release_ready(const qa_console_release *, const qa_console *, qa_error *);
/* True only inside the actual retained program's command callback. */
bool qa_console_release_active(const qa_console_release *);
/* Records entry into real command dispatch, rather than queue installation. */
bool qa_console_release_entered(const qa_console_release *);
/* Refuses while an entered program has pending continuation or failed source
 * history. A refusal retains both the program and its actual console lease. */
bool qa_console_release_abort(qa_console_release *, qa_console_release_outcome *, qa_error *);
typedef enum qa_console_release_disposition {
    QA_CONSOLE_RELEASE_RETIRED_ACTOR,
    QA_CONSOLE_RELEASE_DETACHED_SOURCE
} qa_console_release_disposition;
/* Pure actual-owner qualification. ACTOR must inspect the original registry
 * identity and exact captured generation/slot absence. DETACHED_SOURCE must
 * prove retirement of this physical source/console lifetime while retaining
 * its callback parents. An inactive/stale publication alone proves neither. */
typedef bool (*qa_console_release_retirement_fn)(void *, const qa_console *,
    const qa_command_context *, qa_console_release_disposition, qa_error *);
bool qa_console_release_retirement_ready(const qa_console_release *,
    qa_console_release_disposition, qa_console_release_retirement_fn, void *, qa_error *);
/* Consumes only after the complete parent's pure retirement qualification.
 * Undispatched work is disposed under that actual source lifetime decision;
 * no command, alias, script or handler is replayed. */
void qa_console_release_retirement_publish(qa_console_release *);
void qa_console_release_publish(qa_console_release *);
#endif
