#ifndef QA_TARGETS_H
#define QA_TARGETS_H
#include "qa/session.h"
#include "qa/math.h"
#include "qa/monster_mission.h"

typedef struct qa_authored_target {
    qa_string_id classname, targetname, target, killtarget, message;
    qa_string_id shader_old, shader_new;
    float delay_seconds, wait_seconds;
} qa_authored_target;
typedef enum qa_monster_activation_kind {
    QA_MONSTER_ACTIVE, QA_MONSTER_DORMANT, QA_MONSTER_SCHEDULED
} qa_monster_activation_kind;
typedef enum qa_monster_placement_kind {
    QA_MONSTER_PLACED, QA_MONSTER_WAITING, QA_MONSTER_TELEPORT
} qa_monster_placement_kind;
/* The map owns authored links and encounter state. Native behavior remains in
 * its selected Source; this row is the ordinary shared target authority. */
typedef struct qa_monster_barrier { qa_actor_id actor; qa_vec3 origin; } qa_monster_barrier;
typedef struct qa_authored_monster {
    qa_authored_target fields;
    qa_actor_owner owner;
    qa_ruleset_id source;
    uint32_t ordinal, spawnflags;
    qa_string_id death_target, drop_item, item_target, health_target, route, combat_target;
    qa_actor_id route_goal, combat_goal, activator, previous_corner;
    bool route_resolved, stand_ground, counted_spawn, counted_death;
    qa_monster_activation_kind activation;
    uint64_t activation_ns, follow_until_ns;
    qa_monster_placement_kind placement;
    qa_vec3 authored_origin, placement_origin;
    qa_monster_barrier *barriers;
    size_t barrier_count;
} qa_authored_monster;
typedef struct qa_targets qa_targets;
typedef enum qa_target_field_kind {
    QA_TARGET_FIELD_TEXT,
    QA_TARGET_FIELD_NUMBER,
    QA_TARGET_FIELD_VECTOR
} qa_target_field_kind;
typedef struct qa_target_field {
    qa_target_field_kind kind;
    union {
        qa_string_id text;
        double number;
        qa_vec3 vector;
    } value;
} qa_target_field;
typedef struct qa_target_binding {
    qa_actor_id actor;
    qa_ruleset_id source;
    void *context;
    /* Native and compatibility owners retain their source fields. Read is a
     * nonmutating callback; use may remove, replace or relink any actor. */
    bool (*read)(void *, qa_actor_id, qa_authored_target *);
    bool (*use)(void *, qa_actor_id target, qa_actor_id other, qa_actor_id activator, qa_error *);
    /* Optional source field lookup for authored path and mover metadata.
     * Text belongs to the shared session. Native numeric fields stay typed. */
    bool (*field)(void *, qa_actor_id, const char *key, qa_target_field *value);
    bool (*set_targetname)(void *, qa_actor_id, qa_string_id, qa_error *);
    bool (*set_target)(void *, qa_actor_id, qa_string_id, qa_error *);
    bool (*set_delay)(void *, qa_actor_id, float seconds, qa_error *);
    /* Source killtarget removal may splice an actor out of its native team. */
    bool (*before_remove)(void *, qa_actor_id, qa_error *);
    /* Native source shader targets retain their own AddRemap registry and
     * source clock. This callback belongs to the exact actor binding. */
    bool (*remap_shader)(void *, qa_actor_id source, qa_string_id old_name,
                          qa_string_id new_name, uint64_t time_ns, qa_error *);
} qa_target_binding;
typedef struct qa_target_use {
    qa_actor_id source, activator;
    qa_ruleset_id dialect;
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
/* Provider teardown removes only its own current binding. */
void qa_targets_unbind_context(qa_targets *, qa_actor_id, const void *expected_context);
/* Commit this actor's current targetname and source ordering to the index.
 * Notify at the field store, before any nested target query. */
void qa_targets_changed(qa_targets *, qa_actor_id);
void qa_targets_monsters_configure(qa_targets *, void *context,
    bool (*resolve)(void *, qa_actor_owner, qa_monster_mission *, qa_error *));
bool qa_targets_monster_admit(qa_targets *, qa_actor_id, const qa_authored_monster *, qa_error *);
void qa_targets_monster_route(qa_targets *, qa_actor_id, qa_string_id, qa_actor_id);
qa_authored_monster *qa_targets_monster(qa_targets *, qa_actor_id);
bool qa_targets_monster_lookup(void *targets, qa_actor_id, qa_monster_mission *);
bool qa_targets_read(const qa_targets *, qa_actor_id, qa_authored_target *);
/* The binding owns the field. Name setters notify at the committed store;
 * the wrapper also reconciles a callback that mutates then returns failure. */
bool qa_targets_set_targetname(qa_targets *, qa_actor_id, qa_string_id, qa_error *);
bool qa_targets_set_target(qa_targets *, qa_actor_id, qa_string_id, qa_error *);
bool qa_targets_set_delay(qa_targets *, qa_actor_id, float seconds, qa_error *);
bool qa_targets_field(const qa_targets *, qa_actor_id, const char *key, qa_target_field *value);
/* Numeric native fields stay typed; authored text is parsed at this boundary. */
bool qa_targets_number(const qa_targets *, qa_actor_id, const char *key, double *value);
/* Native vectors or exactly three finite float-range text components.
 * Missing/invalid fields leave the output unchanged. */
bool qa_targets_vector(const qa_targets *, qa_actor_id, const char *key, qa_vec3 *value);
bool qa_targets_first(qa_targets *, qa_string_id name, qa_actor_id *);
/* Zero the cursor before traversal. Each call queries the current index;
 * callbacks may remove, add or rename targets between calls. Tied source slots
 * remain distinct through their shared host slots. */
bool qa_targets_next(qa_targets *, qa_string_id name, qa_target_cursor *, qa_actor_id *);
/* Source-order traversal also includes unnamed authored actors. A zero class
 * accepts every binding; otherwise the owner's current classname must match. */
bool qa_targets_next_authored(qa_targets *, qa_string_id classname, qa_target_cursor *,
                              qa_actor_id *);
bool qa_targets_pick(qa_targets *, qa_string_id name, uint32_t random, size_t maximum_choices,
                     qa_actor_id *);
/* Invoke only this actor's use callback, preserving both source identities.
 * Absent/stale bindings and actors without use are no-ops. Nested callbacks
 * may retire or replace the binding; the replacement is never invoked here. */
bool qa_targets_invoke(qa_targets *, qa_actor_id target, qa_actor_id other,
                       qa_actor_id activator, qa_error *);
/* Source invocation reads current fields. Explicit requests serve authored
 * substitutions and delayed actors; their initial fields are value snapshots.
 */
bool qa_targets_use(qa_targets *, qa_actor_id source, qa_actor_id activator, uint64_t time_ns,
                    qa_error *);
/* Authored substitutions retain their snapshot and honor source delay. */
bool qa_targets_use_request(qa_targets *, const qa_target_use *, qa_error *);
bool qa_targets_use_now(qa_targets *, const qa_target_use *, qa_error *);
#endif
