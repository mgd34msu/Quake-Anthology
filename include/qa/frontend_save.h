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
/* Build the selected original source through the normal frontend constructor.
 * Its private replacement window stays hidden until the imported game is ready. */
typedef struct qa_frontend_original_restore qa_frontend_original_restore;
typedef struct qa_frontend_original_save {
    qa_game_family family;
    union { qa_q1_save_data *q1; qa_q2_save_data *q2; } state;
} qa_frontend_original_save;
/* Once an operation is returned, it consumes and clears the decoded save.
 * Dispose a partially constructed operation even when begin fails. Services
 * remain borrowed until disposal; the active frontend remains installed. */
bool qa_frontend_original_restore_begin(qa_frontend *active,const qa_application_persistence_ops *,
    qa_frontend_original_save *,const char *product,qa_frontend_original_restore **,qa_error *);
/* Call once after each real driver frame, retaining the operation while
 * complete is false. Startup script waits advance before raw import; the
 * candidate receives no gameplay frame before raw state is installed. */
bool qa_frontend_original_restore_advance(qa_frontend_original_restore *,qa_frontend **active,
    bool *complete,qa_frontend **displaced,qa_frontend **retained_candidate,qa_error *);
/* Always consumes the operation. A constructor that rejects cancellation or
 * retirement transfers whole to the distinct empty retained_source output. */
bool qa_frontend_original_restore_dispose(qa_frontend_original_restore *,qa_frontend **retained_source,qa_error *);
#endif
