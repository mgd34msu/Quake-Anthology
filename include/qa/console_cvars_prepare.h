#ifndef QA_CONSOLE_CVARS_PREPARE_H
#define QA_CONSOLE_CVARS_PREPARE_H

#include "qa/console.h"

typedef struct qa_cvars_edit qa_cvars_edit;
typedef enum qa_cvars_edit_kind {
    QA_CVARS_EDIT_REGISTER,
    QA_CVARS_EDIT_SET,
    QA_CVARS_EDIT_SET_CONSOLE,
    QA_CVARS_EDIT_SET_FLAGS,
    QA_CVARS_EDIT_ADD_FLAGS,
    QA_CVARS_EDIT_FULL_SET,
    QA_CVARS_EDIT_STAGE,
    QA_CVARS_EDIT_APPLY_LATCHED,
    QA_CVARS_EDIT_RESET,
    QA_CVARS_EDIT_RESTART,
    QA_CVARS_EDIT_SET_NUMBER,
    QA_CVARS_EDIT_RETAIN_SHARED
} qa_cvars_edit_kind;
typedef struct qa_cvars_edit_command {
    qa_cvars_edit_kind kind;
    const char *name, *value, *description;
    uint32_t flags;
    uint64_t owner;
    bool force;
    float number;
} qa_cvars_edit_command;

/* The ticket owns scalar records, while the registry retains its actual
 * bindings, callbacks and identity. Preparation leaves live values untouched.
 * Views borrow the ticket until its next edit or terminal operation. */
bool qa_cvars_edit_prepare(qa_cvars *, qa_cvars_edit **, qa_error *);
qa_cvars *qa_cvars_edit_registry(const qa_cvars_edit *);
const qa_cvar_view *qa_cvars_edit_find(const qa_cvars_edit *, const char *);
const qa_cvar_view *qa_cvars_edit_at(const qa_cvars_edit *, size_t);
size_t qa_cvars_edit_count(const qa_cvars_edit *);
size_t qa_cvars_edit_handle_count(const qa_cvars_edit *);
bool qa_cvars_edit_apply(qa_cvars_edit *, const qa_cvars_edit_command *, qa_error *);
/* Preparation leases the registry against live mutations and capture until
 * publish or abort; routed candidate commands use the ticket explicitly.
 * Ready validates the retained actual binding capabilities and final bound
 * values, then seals the scalar records. Publish
 * consumes the admitted ticket without callbacks or allocation. Finish drains
 * each actual value binding once with its final value in actual binding
 * insertion order, then the prepared
 * effects and observers, after all other owners are published. */
bool qa_cvars_edit_ready(qa_cvars_edit *, qa_error *);
void qa_cvars_edit_publish(qa_cvars_edit *);
bool qa_cvars_edit_finish(qa_cvars *, qa_error *);
void qa_cvars_edit_abort(qa_cvars_edit *);

#endif
