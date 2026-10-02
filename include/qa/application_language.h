#ifndef QA_APPLICATION_LANGUAGE_H
#define QA_APPLICATION_LANGUAGE_H
#include "qa/application.h"

typedef struct qa_application_language_ticket qa_application_language_ticket;
/* Prepare actual source catalogs for the admitted actor and language. Source
 * owners remain borrowed until commit or abort consumes the ticket. Providers
 * without a source text admission return a null ticket. No language is changed
 * until the caller admits its authoritative settings and commits. */
bool qa_application_language_prepare(qa_application *, qa_actor_id, const char *,
    qa_application_language_ticket **, qa_error *);
/* Pure retained source/catalog/recipient proof for this non-null prepared
 * ticket. It performs no acquisition, source callback or language mutation. */
bool qa_application_language_ready_is(const qa_application_language_ticket *,
    const qa_application *,qa_actor_id,const char *);
void qa_application_language_commit(qa_application_language_ticket *);
void qa_application_language_abort(qa_application_language_ticket *);
#endif
