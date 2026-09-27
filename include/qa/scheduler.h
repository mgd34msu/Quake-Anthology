#ifndef QA_SCHEDULER_H
#define QA_SCHEDULER_H

#include "qa/actors.h"

typedef enum qa_clock_kind {
    QA_CLOCK_NETQUAKE, QA_CLOCK_QUAKEWORLD, QA_CLOCK_Q2_CLASSIC,
    QA_CLOCK_Q2_RERELEASE, QA_CLOCK_Q3
} qa_clock_kind;

typedef enum qa_frame_phase {
    QA_FRAME_ENTRY, QA_CLIENT_COMMAND, QA_ENTITY_PRETHINK, QA_ENTITY_PHYSICS,
    QA_ENTITY_THINK, QA_CLIENT_END_FRAME, QA_FRAME_EXIT
} qa_frame_phase;

typedef struct qa_source_frame {
    qa_actor_owner provider;
    qa_clock_kind kind;
    qa_frame_phase phase;
    uint64_t number;
    uint64_t start_ns;
    uint64_t elapsed_ns;
    uint64_t time_ns;
} qa_source_frame;

typedef enum qa_think_boundary {
    QA_THINK_BEFORE_PHYSICS, QA_THINK_DURING_PHYSICS, QA_THINK_AFTER_PHYSICS
} qa_think_boundary;

typedef bool (*qa_think_fn)(void *context, qa_actor_id actor,
                            const qa_source_frame *frame, qa_error *error);
typedef bool (*qa_think_dispatch_fn)(void *context, qa_think_fn callback,
                                     void *callback_context, qa_actor_id actor,
                                     const qa_source_frame *frame, qa_error *error);

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
                            qa_clock_kind kind, qa_error *error);
/* Reservations are invisible to dispatch. A nonzero retiring_owner reserves
 * that active provider's slot; it must be unregistered before commit. Commit consumes success;
 * abort consumes a pending token. The scheduler must outlive its tokens. */
bool qa_scheduler_prepare(qa_scheduler *, qa_actor_owner, qa_clock_kind, qa_actor_owner retiring_owner,
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
/* Later source-slot additions are visible; additions at/before the cursor wait
 * for the next traversal. Only providers represented by frames are eligible;
 * pending work for other providers remains scheduled. */
bool qa_scheduler_advance(qa_scheduler *scheduler, const qa_source_frame *frames,
                          size_t count, qa_think_boundary boundary, qa_error *error);
bool qa_scheduler_clear(qa_scheduler *scheduler, qa_error *error);
bool qa_scheduler_active(const qa_scheduler *scheduler);
bool qa_scheduler_has_admissions(const qa_scheduler *scheduler);
bool qa_frame_project(const qa_source_frame *frame, qa_actor_owner provider,
                       qa_clock_kind kind, qa_source_frame *out, qa_error *error);

#endif
