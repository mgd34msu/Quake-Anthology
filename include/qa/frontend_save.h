#ifndef QA_FRONTEND_SAVE_H
#define QA_FRONTEND_SAVE_H
#include "qa/frontend.h"
#include "qa/persistence_application.h"
#include "qa/q1_save.h"
/* Backend and filesystem resolvers remain borrowed throughout the operation.
 * Capture stores application state and requires a completed driver boundary.
 * Restore rebuilds presentation through the normal frontend constructors. */
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
typedef struct qa_frontend_q1_restore qa_frontend_q1_restore;
/* Begin owns a genuine fresh frontend and exact application save copy. A
 * partially constructed operation can be returned on failure; dispose it.
 * Services remain borrowed until disposal. The active frontend is untouched. */
bool qa_frontend_q1_restore_begin(qa_frontend *active,const qa_application_persistence_ops *,
    const qa_q1_save_data *,const char *product,qa_frontend_q1_restore **,qa_error *);
/* Call once after each real driver frame, retaining the operation while
 * complete is false. Startup script waits advance before raw import; the
 * candidate receives no gameplay frame before raw state is installed. */
bool qa_frontend_q1_restore_advance(qa_frontend_q1_restore *,qa_frontend **active,
    bool *complete,qa_frontend **displaced,qa_frontend **retained_candidate,qa_error *);
/* Only the final shared capture/publication transaction is admitted. Pending
 * startup phases remain excluded from ordinary save capture. */
bool qa_frontend_q1_restore_capture_ready(const qa_frontend_q1_restore *);
/* Always consumes the operation. A constructor that rejects cancellation or
 * retirement transfers whole to the distinct empty retained_source output. */
bool qa_frontend_q1_restore_dispose(qa_frontend_q1_restore *,qa_frontend **retained_source,qa_error *);
#endif
