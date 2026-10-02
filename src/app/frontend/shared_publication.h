#ifndef QA_FRONTEND_SHARED_PUBLICATION_H
#define QA_FRONTEND_SHARED_PUBLICATION_H
#include "shared_settings.h"
#include "qa/application_language.h"

typedef struct frontend_shared_publication frontend_shared_publication;
/* Final preparation begins only after both real release phases terminate.
 * Failure retains the publication and its scalar parent for checked abort. */
bool frontend_shared_publication_prepare(frontend_shared_settings *,frontend_shared_publication **,qa_error *);
bool frontend_shared_publication_ready(frontend_shared_publication *,qa_error *);
bool frontend_shared_publication_ready_is(const frontend_shared_publication *,
    const frontend_shared_settings *,const qa_frontend *,const qa_application *,const qa_launch_snapshot *);
bool frontend_shared_publication_languages(const frontend_shared_settings *,
    const qa_application_language_ticket *const **,size_t *);
bool frontend_shared_settings_consumed_is(const frontend_shared_settings *,
    const qa_frontend *,const qa_application *,const qa_launch_snapshot *);
void frontend_shared_publication_consume(frontend_shared_publication *);
/* The actual manager scalar slot and flow publication slot are consumed
 * together only after checked native/resource retirement and scalar observers.
 * Terminal errors still require both slots NULL and complete=true. */
bool frontend_shared_publication_finish(frontend_shared_settings **,
    frontend_shared_publication **,bool *complete,qa_error *);
/* Checked resource cancellation leaves the parent scalar owner retained for
 * the existing candidate abort. Both native windows survive input cleanup. */
bool frontend_shared_publication_abort(frontend_shared_publication **,qa_error *);
#endif
