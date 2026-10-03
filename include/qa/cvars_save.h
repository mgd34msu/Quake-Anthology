#ifndef QA_CVARS_SAVE_H
#define QA_CVARS_SAVE_H
#include "qa/console.h"

typedef struct qa_cvars_restore qa_cvars_restore;
/* Exact registry order, handles, ownership, values, latches and metadata.
 * Callback addresses are rebuilt from the isolated candidate registry.
 * Candidate bindings must already exist with matching names and owners.
 * A registry must outlive its outstanding tickets. Capture is callback-free;
 * prepare may call candidate binding validators and owns all allocations.
 * Commit consumes a validated ticket without allocation or notifications. */
bool qa_cvars_save_capture(const qa_cvars *, qa_buffer *, qa_error *);
bool qa_cvars_save_prepare(qa_cvars *, qa_bytes, qa_cvars_restore **, qa_error *);
bool qa_cvars_save_validate(const qa_cvars_restore *, qa_error *);
/* Prepared physical rows remain readable until commit or abort. */
const qa_cvar_view *qa_cvars_save_find(const qa_cvars_restore *, const char *);
bool qa_cvars_save_commit(qa_cvars_restore *, qa_error *);
void qa_cvars_save_abort(qa_cvars_restore *);
#endif
