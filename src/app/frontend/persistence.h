#ifndef QA_FRONTEND_PERSISTENCE_H
#define QA_FRONTEND_PERSISTENCE_H
#include "qa/frontend.h"
#include "qa/persistence_application.h"

/* Backend/file resolvers remain borrowed for the operation. The frontend owns
 * the seven external records and its actual candidate constructor callbacks.
 * A failed candidate whose children reject retirement transfers to retained;
 * its frontend context must remain alive until ordinary destroy succeeds. */
bool frontend_persistence_capture(qa_frontend *, const qa_application_persistence_ops *,
    qa_save_purpose, qa_save_image **, qa_error *);
bool frontend_persistence_restore(qa_frontend **, const qa_application_persistence_ops *,
    const qa_save_image *, qa_frontend **displaced, qa_frontend **retained, qa_error *);
#endif
