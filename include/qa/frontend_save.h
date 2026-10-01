#ifndef QA_FRONTEND_SAVE_H
#define QA_FRONTEND_SAVE_H
#include "qa/frontend.h"
#include "qa/persistence_application.h"
#include "qa/q1_save.h"
/* Backend and filesystem resolvers remain borrowed throughout the operation.
 * The concrete frontend supplies all seven external owner records. Capture
 * requires a completed driver boundary, after every callback has returned. */
bool qa_frontend_persistence_capture(qa_frontend *, const qa_application_persistence_ops *,
    qa_save_purpose, qa_save_image **, qa_error *);
/* active/displaced stay unchanged on failure. A failed candidate whose actual
 * children reject retirement transfers to retained; retry ordinary destroy
 * while preserving its frontend callback context. No driver may be using the
 * active pointer during this call; in-run requests drain at a driver boundary. */
bool qa_frontend_persistence_restore(qa_frontend **active, const qa_application_persistence_ops *,
    const qa_save_image *, qa_frontend **displaced, qa_frontend **retained, qa_error *);
/* Build the genuine selected original source and fresh frontend owners, then
 * publish their shared continuation through the same native transaction. Both
 * failed source and final candidate graphs remain independently reachable.
 * Publication may succeed while source retirement transfers to retained_source. */
bool qa_frontend_q1_save_restore(qa_frontend **active,const qa_application_persistence_ops *,
    const qa_q1_save_data *,const char *product,qa_frontend **displaced,
    qa_frontend **retained_source,qa_frontend **retained_candidate,qa_error *);
#endif
