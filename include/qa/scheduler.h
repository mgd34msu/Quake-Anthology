#ifndef QA_SCHEDULER_H
#define QA_SCHEDULER_H

#include "qa/ruleset.h"
#include "qa/actors.h"

typedef enum qa_frame_phase {
    QA_FRAME_ENTRY, QA_CLIENT_COMMAND, QA_ENTITY_PRETHINK, QA_ENTITY_PHYSICS,
    QA_ENTITY_THINK, QA_CLIENT_END_FRAME, QA_FRAME_EXIT
} qa_frame_phase;

typedef struct qa_source_frame {
    qa_actor_owner provider;
    qa_ruleset_id kind;
    qa_frame_phase phase;
    uint64_t number;
    uint64_t start_ns;
    uint64_t elapsed_ns;
    uint64_t time_ns;
} qa_source_frame;

/* A usercmd is a distinct source admission. Its current source time and
 * completed world counter never imply another world frame has run. */
typedef struct qa_source_command {
    qa_actor_id actor;
    qa_actor_owner provider;
    qa_ruleset_id kind;
    qa_frame_phase phase;
    uint64_t completed_frame_number, time_ns, elapsed_ns, host_elapsed_ns;
} qa_source_command;

typedef enum qa_think_boundary {
    QA_THINK_BEFORE_PHYSICS, QA_THINK_DURING_PHYSICS, QA_THINK_AFTER_PHYSICS
} qa_think_boundary;

typedef enum qa_think_scope_kind {
    QA_THINK_WORLD_FRAME, QA_THINK_SOURCE_COMMAND
} qa_think_scope_kind;
typedef struct qa_think_scope {
    qa_think_scope_kind kind;
    union {
        qa_source_frame frame;
        qa_source_command command;
    } source;
    /* Due-time clamp for this callback; the admission keeps its own time. */
    uint64_t time_ns;
    uint64_t interval_start_ns, interval_elapsed_ns;
} qa_think_scope;

typedef bool (*qa_think_fn)(void *context, qa_actor_id actor,
                            const qa_think_scope *scope, qa_error *error);
typedef bool (*qa_think_dispatch_fn)(void *context, qa_think_fn callback,
                                     void *callback_context, qa_actor_id actor,
                                     const qa_think_scope *scope, qa_error *error);

typedef struct qa_think {
    qa_actor_id actor;
    qa_actor_owner execution_provider;
    uint32_t callback_id;
    uint64_t due_ns;
    uint64_t sequence;
    qa_think_boundary boundary;
    qa_think_fn callback;
    void *context;
} qa_think;

typedef struct qa_think_result {
    uint64_t invocations;
    bool alive;
} qa_think_result;

typedef struct qa_scheduler qa_scheduler;
typedef struct qa_scheduler_admission qa_scheduler_admission;

/* Mixed ordering compares provider registration order first. Both modes then
 * compare source slot and think sequence; provider order and host slot break
 * exact ties. Registry ownership remains with the caller. */
bool qa_scheduler_create(qa_actor_registry *actors, uint32_t provider_capacity,
                          bool mixed_order, qa_think_dispatch_fn dispatch,
                          void *context, qa_scheduler **out, qa_error *error);
bool qa_scheduler_destroy(qa_scheduler *scheduler, qa_error *error);
bool qa_scheduler_register(qa_scheduler *scheduler, qa_actor_owner provider,
                            qa_ruleset_id kind, qa_error *error);
/* Reservations are invisible to dispatch. A nonzero retiring_owner reserves
 * that active provider's slot; it must be unregistered before commit. Commit consumes success;
 * abort consumes a pending token. The scheduler must outlive its tokens. */
bool qa_scheduler_prepare(qa_scheduler *, qa_actor_owner, qa_ruleset_id, qa_actor_owner retiring_owner,
                           qa_scheduler_admission **, qa_error *);
bool qa_scheduler_admission_validate(qa_scheduler_admission *, qa_error *);
bool qa_scheduler_admission_commit(qa_scheduler_admission *, qa_error *);
void qa_scheduler_admission_abort(qa_scheduler_admission *);
bool qa_scheduler_unregister(qa_scheduler *scheduler, qa_actor_owner provider,
                              qa_error *error);
bool qa_scheduler_schedule(qa_scheduler *scheduler, const qa_think *think, qa_error *error);
void qa_scheduler_cancel(qa_scheduler *scheduler, qa_actor_id actor);
const qa_think *qa_scheduler_pending(qa_scheduler *scheduler, qa_actor_id actor);
/* Clears before dispatch. QW rechecks a same-actor reschedule until not due. */
bool qa_scheduler_run(qa_scheduler *scheduler, qa_actor_id actor,
                      const qa_source_frame *frame, qa_think_boundary boundary,
                      qa_think_result *out, qa_error *error);
/* Source callbacks that run once per actor turn keep the real execution clock
 * and due-time clamp, but defer a same-actor reschedule to its next turn. */
bool qa_scheduler_run_once(qa_scheduler *, qa_actor_id, const qa_source_frame *,
    qa_think_boundary, qa_think_result *, qa_error *);
/* Executes one pending source callback in an actual usercmd admission.
 * source_time_ns is the callback owner's retained gameplay clock. The
 * command's admission clock and physical interval remain separate. */
bool qa_scheduler_run_command_once(qa_scheduler *, qa_actor_id, const qa_source_command *,
    uint64_t source_time_ns, uint64_t source_elapsed_ns,
    qa_think_boundary, qa_think_result *, qa_error *);
/* Later source-slot additions are visible; additions at/before the cursor wait
 * for the next traversal. Only providers represented by frames are eligible;
 * pending work for other providers remains scheduled. */
bool qa_scheduler_advance(qa_scheduler *scheduler, const qa_source_frame *frames,
                          size_t count, qa_think_boundary boundary, qa_error *error);
bool qa_scheduler_clear(qa_scheduler *scheduler, qa_error *error);
bool qa_scheduler_active(const qa_scheduler *scheduler);
bool qa_scheduler_has_admissions(const qa_scheduler *scheduler);
typedef struct qa_scheduler_provider_checkpoint {
    qa_actor_owner owner;
    qa_ruleset_id kind;
    uint64_t order;
} qa_scheduler_provider_checkpoint;
typedef struct qa_scheduler_think_checkpoint {
    qa_saved_actor_id actor;
    qa_actor_owner execution_provider;
    uint32_t callback_id;
    uint64_t due_ns, sequence;
    qa_think_boundary boundary;
} qa_scheduler_think_checkpoint;
typedef struct qa_scheduler_checkpoint {
    qa_scheduler_provider_checkpoint *providers;
    qa_scheduler_think_checkpoint *thinks;
    size_t provider_count, think_count;
    uint64_t next_order;
    bool mixed_order;
} qa_scheduler_checkpoint;
typedef bool (*qa_think_resolve_fn)(void *, qa_actor_owner, qa_actor_id, uint32_t,
                                    qa_think_fn *, void **callback_context, qa_error *);
bool qa_scheduler_checkpoint_capture(const qa_scheduler *, qa_scheduler_checkpoint *, qa_error *);
/* Isolated candidate only. All providers must already be registered. Resolver
 * maps declared callback identities to the restored provider's state, never a
 * saved address. Validation/preparation finishes before pending work changes. */
bool qa_scheduler_checkpoint_restore(qa_scheduler *, const qa_scheduler_checkpoint *,
                                      qa_think_resolve_fn, void *, qa_error *);
void qa_scheduler_checkpoint_free(qa_scheduler_checkpoint *);
bool qa_frame_project(const qa_source_frame *frame, qa_actor_owner provider,
                       qa_ruleset_id kind, qa_source_frame *out, qa_error *error);

#endif
