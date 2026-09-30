#ifndef QA_OPERATION_H
#define QA_OPERATION_H

#include "qa/actors.h"

typedef struct qa_operation qa_operation;
typedef struct qa_operation_admission qa_operation_admission;
typedef uint64_t qa_operation_registration;
/* Retain a continuation value only while its operation exists. Invocation
 * identities are checked, so calling it after the callback returns fails. */
typedef struct qa_operation_next { qa_operation *operation; uint64_t invocation; } qa_operation_next;
typedef bool (*qa_operation_canonical_fn)(void *context, const void *request, void *result, qa_error *error);
typedef bool (*qa_operation_transform_fn)(void *context, void *request, qa_error *error);
typedef bool (*qa_operation_observe_fn)(void *context, const void *request, const void *result, qa_error *error);
typedef bool (*qa_operation_replace_fn)(void *context, const void *request, qa_operation_next next,
                                       void *result, qa_error *error);
typedef bool (*qa_operation_committed_fn)(void *context, qa_error *error);
typedef enum qa_operation_hook_kind { QA_OPERATION_TRANSFORM, QA_OPERATION_OBSERVE, QA_OPERATION_REPLACE } qa_operation_hook_kind;
typedef struct qa_operation_hook {
    qa_actor_owner owner;
    uint32_t name; /* Session-interned component-local registration name. */
    int32_t order;
    qa_operation_hook_kind kind;
    void *context;
    union {
        qa_operation_transform_fn transform;
        qa_operation_observe_fn observe;
        qa_operation_replace_fn replace;
    } call;
} qa_operation_hook;

/* One thread owns the operation. Request/result layout is chosen once by the
 * typed gameplay owner. Hook registration allocates; ordinary dispatch reuses
 * retained invocation storage. No-hook dispatch calls canonical directly. */
bool qa_operation_create(size_t request_size, size_t result_size, qa_operation **out, qa_error *error);
bool qa_operation_destroy(qa_operation *operation, qa_error *error);
bool qa_operation_destroy_validate(const qa_operation *operation, qa_error *error);
bool qa_operation_register(qa_operation *operation, const qa_operation_hook *hook,
                           qa_operation_registration *out, qa_error *error);
/* Prepare reserves a hook without exposing it to dispatch. A nonzero retiring
 * registration reserves its replacement and must be removed before commit.
 * Commit publishes without allocation/callbacks and consumes success; failure
 * retains the token. Abort consumes it. The operation must outlive its tokens. */
bool qa_operation_prepare(qa_operation *, const qa_operation_hook *,
                           qa_operation_registration retiring, qa_operation_admission **, qa_error *);
bool qa_operation_admission_validate(qa_operation_admission *, qa_error *);
bool qa_operation_admission_commit(qa_operation_admission *, qa_operation_registration *, qa_error *);
void qa_operation_admission_abort(qa_operation_admission *);
bool qa_operation_unregister(qa_operation *operation, qa_operation_registration registration);
void qa_operation_remove_owner(qa_operation *operation, qa_actor_owner owner);
void qa_operation_clear(qa_operation *operation);
bool qa_operation_active(const qa_operation *operation);
/* New registrations join the next invocation; removals take effect immediately
 * even during nested calls. Observers see the request used by canonical. */
bool qa_operation_dispatch(qa_operation *operation, const void *request, void *result,
                           qa_operation_canonical_fn canonical, void *canonical_context,
                           qa_operation_committed_fn committed, void *committed_context, qa_error *error);
/* Exactly one synchronous use is allowed. A repeated/failed continuation makes
 * the enclosing dispatch fail even if the replacement ignores that failure. */
bool qa_operation_continue(qa_operation_next next, const void *request, void *result, qa_error *error);

#endif
