#ifndef QA_CVARS_SAVE_H
#define QA_CVARS_SAVE_H
#include "qa/console.h"

typedef struct qa_cvars_restore qa_cvars_restore;
/* Gameplay name/value/latch state only. The current factory supplies declarations,
 * aliases, handles and callbacks. Prepare merges into its existing edit kernel;
 * commit publishes and delivers current notifications. Successful commit consumes
 * the ticket; failure leaves it available for checked abort. */
bool qa_cvars_save_capture(const qa_cvars *, qa_buffer *, qa_error *);
bool qa_cvars_save_matches(qa_cvars *, qa_bytes, qa_error *);
bool qa_cvars_save_prepare(qa_cvars *, qa_bytes, qa_cvars_restore **, qa_error *);
bool qa_cvars_save_validate(const qa_cvars_restore *, qa_error *);
/* Prepared physical rows remain readable until commit or abort. */
const qa_cvar_view *qa_cvars_save_find(const qa_cvars_restore *, const char *);
bool qa_cvars_save_commit(qa_cvars_restore *, qa_error *);
void qa_cvars_save_abort(qa_cvars_restore *);
#endif
