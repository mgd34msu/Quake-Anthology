#ifndef QA_SESSION_H
#define QA_SESSION_H

#include "qa/scheduler.h"
#include "qa/strings.h"

typedef struct qa_session qa_session;
typedef void (*qa_cleanup_fn)(void *context);

typedef struct qa_clock_config {
    qa_clock_kind kind;
    uint64_t initial_time_ns;
    uint64_t interval_ns;
    uint64_t minimum_frame_ns;
    uint64_t maximum_frame_ns;
    /* Initial source lead, such as Q2's first tick ahead of host time. */
    uint64_t initial_lead_ns;
    /* Zero is unlimited. Unprocessed debt always survives a bounded advance. */
    uint32_t maximum_steps;
} qa_clock_config;

typedef struct qa_clock_state {
    uint64_t host_origin_ns;
    uint64_t elapsed_ns;
    uint64_t debt_ns;
    uint64_t frame_number;
    qa_source_frame frame;
    bool paused;
} qa_clock_state;

typedef bool (*qa_component_frame_fn)(void *state, qa_session *session,
                                      const qa_source_frame *frame, qa_error *error);
typedef bool (*qa_component_actor_fn)(void *state, qa_session *session,
                                      qa_actor_id actor, const qa_source_frame *frame,
                                      qa_error *error);
typedef void (*qa_component_release_fn)(void *state, qa_session *session,
                                        qa_actor_record released);

typedef struct qa_component {
    /* Intern this name in qa_session_strings before registration. */
    qa_actor_owner owner;
    qa_clock_config clock;
    void *state;
    qa_cleanup_fn close;
    qa_component_frame_fn begin_frame;
    qa_component_actor_fn actor_frame;
    qa_component_frame_fn end_frame;
    qa_component_release_fn actor_released;
} qa_component;

typedef enum qa_invocation_kind {
    QA_INVOKE_THINK, QA_INVOKE_TOUCH, QA_INVOKE_USE, QA_INVOKE_PAIN,
    QA_INVOKE_DIE, QA_INVOKE_PHYSICS
} qa_invocation_kind;

typedef struct qa_invocation {
    qa_actor_id actor;
    qa_invocation_kind kind;
    const struct qa_invocation *parent;
} qa_invocation;

typedef bool (*qa_invocation_fn)(void *context, qa_session *session, qa_error *error);
typedef bool (*qa_session_release_fn)(void *context, qa_session *session,
                                      qa_actor_record released, qa_error *error);

typedef struct qa_session_options {
    uint32_t actor_capacity;
    uint32_t component_capacity;
    bool mixed_order;
    /* Shared body/link/collision cleanup runs before component notification. */
    qa_session_release_fn actor_released;
    /* Runs once per due source and live actor, in source registration order,
     * before the actor's selected physics. Receives that source's own clock. */
    qa_component_actor_fn source_actor;
    qa_component_actor_fn after_actor;
    void *release_context;
} qa_session_options;

qa_clock_config qa_clock_defaults(qa_clock_kind kind);
bool qa_session_create(const qa_session_options *options, qa_session **out, qa_error *error);
/* Requires a safe point and no component or scheduler admissions. If ready,
 * destruction consumes the session even when false reports a cleanup error.
 * Otherwise failure leaves the session intact. NULL is ready to destroy. */
bool qa_session_destroy_ready(const qa_session *session);
bool qa_session_destroy(qa_session *session, qa_error *error);
/* Component state transfers only on successful add. Removal retires its actors
 * and scheduling before close; foreign actors must first rebind their execution. */
bool qa_session_add(qa_session *session, const qa_component *component, qa_error *error);
typedef struct qa_component_admission qa_component_admission;
/* Prepare reserves capacity without registering callbacks. A nonzero retiring_owner
 * reserves that active owner's slot; remove it before commit. This also supports
 * switching to a different owner at full capacity. Commit and abort consume tokens.
 * Commit allocates nothing, calls no provider, and retains the token on failure. */
bool qa_session_prepare_component(qa_session *, const qa_component *, qa_actor_owner retiring_owner,
                                   qa_component_admission **, qa_error *);
bool qa_component_admission_validate(qa_component_admission *, qa_error *);
bool qa_component_admission_commit(qa_component_admission *, qa_error *);
void qa_component_admission_abort(qa_component_admission *);
bool qa_session_remove(qa_session *session, qa_actor_owner owner, qa_error *error);
bool qa_session_pause(qa_session *session, qa_actor_owner owner, bool paused, qa_error *error);
bool qa_session_clock(const qa_session *session, qa_actor_owner owner, qa_clock_state *out);
bool qa_session_restore_clock(qa_session *session, qa_actor_owner owner,
                              const qa_clock_state *state, qa_error *error);
/* Elapsed time is explicit. The engine never reads wall time here. A callback
 * failure faults the session after already committed mutations; it is not retried. */
bool qa_session_advance(qa_session *session, uint64_t elapsed_ns, qa_error *error);
bool qa_session_safe(const qa_session *session);
bool qa_session_faulted(const qa_session *session);
const qa_error *qa_session_error(const qa_session *session);
uint64_t qa_session_elapsed(const qa_session *session);
bool qa_session_restore_elapsed(qa_session *session, uint64_t elapsed_ns, qa_error *error);
typedef struct qa_session_component_checkpoint {
    qa_actor_owner owner;
    qa_clock_config config;
    qa_clock_state state;
    uint64_t order;
} qa_session_component_checkpoint;
typedef struct qa_session_execution_checkpoint {
    qa_saved_actor_id actor;
    qa_actor_owner provider;
} qa_session_execution_checkpoint;
typedef struct qa_session_checkpoint {
    qa_session_component_checkpoint *components;
    qa_session_execution_checkpoint *executions;
    size_t component_count, execution_count;
    uint64_t elapsed_ns, next_order;
    uint32_t actor_capacity, component_capacity;
    bool mixed_order;
    qa_scheduler_checkpoint scheduler;
} qa_session_checkpoint;
/* Fresh candidate before shared services borrow actors or strings. The supplied
 * exact saved string table transfers only on success. Definitions/provider IDs
 * refer to that table; actor history enters a fresh registry namespace. */
bool qa_session_create_restored(const qa_session_options *, const qa_actor_checkpoint *,
                                 qa_strings *owned_strings, qa_session **, qa_error *);
bool qa_session_checkpoint_capture(qa_session *, qa_session_checkpoint *, qa_error *);
/* Isolated candidate after providers and actor ownership have been rebuilt. */
bool qa_session_checkpoint_restore(qa_session *, const qa_session_checkpoint *,
                                    qa_think_resolve_fn, void *, qa_error *);
void qa_session_checkpoint_free(qa_session_checkpoint *);
const qa_actor_registry *qa_session_actors(const qa_session *session);
/* Mutable borrow for the shared world service. Gameplay uses session allocation
 * and release. Direct allocations must use registered owners and cannot run
 * during component retirement. Unaccounted allocations are reconciled into the
 * retained traversal index before the next actor turn; releases are immediate. */
qa_actor_registry *qa_session_actor_registry(qa_session *session);
qa_scheduler *qa_session_scheduler(qa_session *session);
/* One retained table serves providers, definitions, items, teams, and services.
 * IDs and text survive world replacement; the session destroys the table. */
qa_strings *qa_session_strings(qa_session *session);
bool qa_session_allocate(qa_session *session, qa_actor_owner owner,
                          qa_actor_definition definition, bool has_source,
                          uint32_t source_slot, qa_actor_id *out, qa_error *error);
bool qa_session_release(qa_session *session, qa_actor_id actor, qa_error *error);
/* Cancel a pending think from the previous provider before rebinding; callback
 * state and source time cannot be implicitly migrated between providers. */
bool qa_session_bind_execution(qa_session *session, qa_actor_id actor,
                                qa_actor_owner provider, qa_error *error);
bool qa_session_execution(const qa_session *session, qa_actor_id actor,
                           qa_actor_owner *out);
bool qa_session_schedule(qa_session *session, const qa_think *think, qa_error *error);
const qa_invocation *qa_session_current(const qa_session *session);
bool qa_session_invoke(qa_session *session, qa_actor_id actor, qa_invocation_kind kind,
                        qa_invocation_fn callback, void *context, qa_error *error);
void *qa_session_world(const qa_session *session);
/* A restored session accepts its initial world without retiring the restored
 * actor namespace or clocks. Success transfers ownership exactly once;
 * failure retains caller ownership. Ordinary sessions cannot use this path. */
bool qa_session_adopt_restored_world(qa_session *, void *, qa_cleanup_fn, qa_error *);
/* Candidate is fully prepared by the caller. Failure retains caller ownership.
 * At a safe point, retire old actors, publish candidate, then close old world.
 * A cleanup error before publication faults the old session; retired actors are
 * not resurrected. Connections and seats stay outside this world swap. */
bool qa_session_replace_world(qa_session *session, void *candidate,
                               qa_cleanup_fn close, qa_error *error);
/* Destructive map travel with a stable world object: releases every actor,
 * clears scheduled thinks and resets provider clocks. A release failure faults
 * the session after already committed retirements; world ownership is unchanged. */
bool qa_session_retire_world(qa_session *, qa_error *);

#endif
