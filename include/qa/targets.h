#ifndef QA_TARGETS_H
#define QA_TARGETS_H
#include "qa/session.h"

typedef struct qa_authored_target {
    qa_string_id classname, targetname, target, killtarget, message;
    qa_string_id shader_old, shader_new;
    float delay_seconds;
} qa_authored_target;
typedef struct qa_targets qa_targets;
typedef struct qa_target_binding {
    qa_actor_id actor;
    qa_clock_kind source;
    void *context;
    /* Native and compatibility owners retain their source fields. Read is a
     * nonmutating callback; use may remove, replace or relink any actor. */
    bool (*read)(void *, qa_actor_id, qa_authored_target *);
    bool (*use)(void *, qa_actor_id target, qa_actor_id other, qa_actor_id activator, qa_error *);
} qa_target_binding;
typedef struct qa_target_use {
    qa_actor_id source, activator;
    qa_clock_kind dialect;
    qa_authored_target fields;
    uint64_t time_ns;
    /* Refresh fields after nested callbacks only for ordinary source use.
     * Explicit substitutions and delayed snapshots leave this false. */
    bool live_fields;
} qa_target_use;
typedef struct qa_target_cursor {
    uint32_t source_order, host_slot;
    bool started;
} qa_target_cursor;
typedef struct qa_target_options {
    qa_session *session;
    void *context;
    /* A source delay owns a real DelayedUse actor and copies the supplied
     * fields/activator. Its source think later calls qa_targets_use_now with
     * that delayed actor as source. This preserves source entity scheduling. */
    bool (*defer)(void *, const qa_target_use *, qa_error *);
    bool (*message)(void *, const qa_target_use *, qa_error *);
    bool (*remap_shader)(void *, qa_string_id old_name, qa_string_id new_name, uint64_t time_ns,
                         qa_error *);
    void (*diagnostic)(void *, qa_actor_id, const char *);
} qa_target_options;
qa_targets *qa_targets_create(const qa_target_options *, qa_error *);
void qa_targets_destroy(qa_targets *);
bool qa_targets_bind(qa_targets *, const qa_target_binding *, qa_error *);
void qa_targets_unbind(qa_targets *, qa_actor_id);
/* Call after changing a bound actor's targetname or source-slot mapping. The
 * retained index rebuilds only after such changes or registry mutations. */
void qa_targets_changed(qa_targets *);
bool qa_targets_read(const qa_targets *, qa_actor_id, qa_authored_target *);
bool qa_targets_first(qa_targets *, qa_string_id name, qa_actor_id *);
/* Zero the cursor before traversal. Each call queries the current index;
 * callbacks may remove, add or rename targets between calls. Tied source slots
 * remain distinct through their shared host slots. */
bool qa_targets_next(qa_targets *, qa_string_id name, qa_target_cursor *, qa_actor_id *);
bool qa_targets_pick(qa_targets *, qa_string_id name, uint32_t random, size_t maximum_choices,
                     qa_actor_id *);
/* Source invocation reads current fields. Explicit requests serve authored
 * substitutions and delayed actors; their initial fields are value snapshots. */
bool qa_targets_use(qa_targets *, qa_actor_id source, qa_actor_id activator, uint64_t time_ns,
                    qa_error *);
bool qa_targets_use_now(qa_targets *, const qa_target_use *, qa_error *);
#endif
