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
    QA_CVARS_EDIT_RETAIN_SHARED,
    QA_CVARS_EDIT_ASSIGN,
    QA_CVARS_EDIT_SAVE_POLICY
} qa_cvars_edit_kind;
/* Forced direct scalar assignment, as QW serverinfo assigns a registered
 * cvar string/value without Cvar_Set notifications or protocol propagation.
 * Keeps flags, latched values, bindings and previously queued edit effects. */
bool qa_cvars_assign(qa_cvars *, const char *, const char *, qa_ruleset_id, qa_error *);

typedef struct qa_cvars_edit_command {
    qa_cvars_edit_kind kind;
    const char *name, *value, *description;
    uint32_t flags;
    uint64_t owner;
    bool force;
    float number;
    qa_ruleset_id source_dialect; /* Direct assignment numeric grammar. */
    qa_cvar_save_policy save_policy;
} qa_cvars_edit_command;

/* Applies a live operation through the same scalar kernel as an edit ticket,
 * retaining the live registry's notification and observer drain boundary. */
bool qa_cvars_apply(qa_cvars *, const qa_cvars_edit_command *, qa_error *);

/* The ticket owns scalar records, while the registry retains its actual
 * bindings, callbacks and identity. Preparation leaves live values untouched.
 * Views borrow the ticket until its next edit or terminal operation. */
bool qa_cvars_edit_prepare(qa_cvars *, qa_cvars_edit **, qa_error *);
qa_cvars *qa_cvars_edit_registry(const qa_cvars_edit *);
/* Pure borrow of this canonical owner's existing ticket; terminal ownership
 * remains with the caller that prepared it. */
qa_cvars_edit *qa_cvars_prepared_edit(const qa_cvars *shared);
/* Callback nesting retains the shared table's one prepared ticket. All views
 * read and write those values without needing a separate entered context.
 * Leaving does not publish, finish or abort a borrowed ticket. */
bool qa_cvars_edit_enter(qa_cvars_edit *, qa_cvars *, qa_error *);
bool qa_cvars_edit_leave(qa_cvars_edit *, qa_cvars *, qa_error *);
const qa_cvar_view *qa_cvars_edit_find(const qa_cvars_edit *, const char *);
const qa_cvar_view *qa_cvars_edit_at(const qa_cvars_edit *, size_t);
size_t qa_cvars_edit_count(const qa_cvars_edit *);
size_t qa_cvars_edit_handle_count(const qa_cvars_edit *);
/* Uses the same native archive selection and config text formatting as the
 * live registry, with the actual prepared records in their retained order. */
bool qa_cvars_edit_config_filtered(const qa_cvars_edit *,qa_cvar_config_filter,
    void *,qa_buffer *,qa_error *);
/* Archive eligibility of a genuine canonical edit_at record, with the same
 * native latched-value policy as published archive values. */
const char *qa_cvars_edit_archive_value(const qa_cvars_edit *,const qa_cvar_view *);
/* Resolve a name to its actual physical canonical prepared record without
 * projecting converted aliases or mutating their cached views. */
const qa_cvar_view *qa_cvars_edit_canonical_record(const qa_cvars_edit *,const char *);
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
/* Pure returned callback proof for this exact retained registry lease, before
 * or after sealing. This does not validate or admit scalar publication. */
bool qa_cvars_edit_returned_is(const qa_cvars_edit *, const qa_cvars *);
/* Cancellation-only retained lease proof, including a faulted preparation.
 * It admits disposal after callbacks return, never scalar publication. */
bool qa_cvars_edit_abort_is(const qa_cvars_edit *, const qa_cvars *);
/* Pure proof of the same successfully sealed ticket and retained bindings.
 * Does not rerun validators or publish values. */
bool qa_cvars_edit_ready_is(const qa_cvars_edit *);
void qa_cvars_edit_publish(qa_cvars_edit *);
bool qa_cvars_edit_finish(qa_cvars *, qa_error *);
void qa_cvars_edit_abort(qa_cvars_edit *);

#endif
