/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QA_WORLD_H
#define QA_WORLD_H

#include "qa/collision.h"

typedef struct qa_world qa_world;
typedef struct qa_body_state {
    qa_vec3 origin, angles, velocity;
    qa_bounds bounds;
    qa_actor_id ground;
} qa_body_state;
typedef struct qa_linked_body {
    qa_actor_id actor;
    qa_body_state state;
    qa_bounds absolute_bounds;
    uint64_t link_count;
} qa_linked_body;
typedef enum qa_body_follow { QA_BODY_FOLLOW_TRANSLATION, QA_BODY_FOLLOW_CENTER, QA_BODY_FOLLOW_BOUNDS_MIN } qa_body_follow;
typedef struct qa_body_attachment { qa_actor_id anchor; qa_body_follow follow; qa_vec3 offset; } qa_body_attachment;
typedef enum qa_collision_role { QA_COLLISION_SOLID, QA_COLLISION_TRIGGER, QA_COLLISION_BOTH } qa_collision_role;
typedef struct qa_actor_collision {
    qa_collision_family family;
    qa_shape_kind shape; /* BOX or CAPSULE for temporary bodies; ignored for inline models. */
    bool inline_model;
    uint32_t model;
    int32_t contents;
    qa_actor_id owner;
    qa_collision_role role;
    bool monster, dead_monster, q1_corpse;
    bool has_q3_owner;
    int32_t q3_entity_number, q3_owner_number;
} qa_actor_collision;
typedef struct qa_spatial_actor { qa_linked_body body; qa_actor_collision collision; } qa_spatial_actor;
typedef struct qa_body_binding {
    void *context;
    bool (*read)(void *, qa_body_state *, qa_error *);
    bool (*write)(void *, const qa_body_state *, qa_error *);
    void (*linked)(void *, const qa_linked_body *);
} qa_body_binding;
typedef struct qa_world_hooks {
    void *context;
    /* NULL uses translated bounds, with no family-specific padding. Providers
     * choose their source padding/rotation here; bounds are captured on link. */
    bool (*absolute_bounds)(void *, qa_actor_id, const qa_body_state *, qa_bounds *, qa_error *);
    void (*linked)(void *, const qa_linked_body *);
    void (*unlinked)(void *, qa_actor_id);
} qa_world_hooks;
typedef struct qa_body_link_state { uint64_t link_count; bool linked; qa_body_state state; qa_bounds absolute_bounds; } qa_body_link_state;

/* The session owns actors and geometry. World borrows both; destruction never
 * clears/releases the registry. Calls and callbacks have one thread owner.
 * Callbacks may mutate actors/links; destroying the world within one is rejected. */
bool qa_world_create(qa_actor_registry *, qa_collision_geometry *, const qa_world_hooks *, qa_world **, qa_error *);
bool qa_world_destroy(qa_world *, qa_error *);
qa_actor_registry *qa_world_actors(qa_world *);
qa_collision_geometry *qa_world_geometry(qa_world *);
/* The session must forward every registry release here, after invalidation,
 * before provider teardown. It unlinks the released generation and releases
 * attached children through the same registry, including nested attachments. */
bool qa_world_actor_released(qa_world *, qa_actor_record, qa_error *);
bool qa_world_body_create(qa_world *, qa_actor_id, const qa_body_state *, qa_error *);
bool qa_world_body_bind(qa_world *, qa_actor_id, const qa_body_binding *, bool replace, qa_error *);
bool qa_world_body_read(qa_world *, qa_actor_id, qa_body_state *, qa_error *);
bool qa_world_body_write(qa_world *, qa_actor_id, const qa_body_state *, qa_error *);
bool qa_world_set_collision(qa_world *, qa_actor_id, const qa_actor_collision *, qa_error *); /* NULL disables collision. */
bool qa_world_get_collision(const qa_world *, qa_actor_id, qa_actor_collision *);
bool qa_world_attach(qa_world *, qa_actor_id, const qa_body_attachment *, qa_error *);
bool qa_world_detach(qa_world *, qa_actor_id, qa_error *);
bool qa_world_attachment(const qa_world *, qa_actor_id, qa_body_attachment *);
/* Start cursor at zero. Ordered iteration supports checkpointing attachment
 * insertion order independently of actor host-slot allocation. */
bool qa_world_next_attachment(const qa_world *, uint64_t *cursor, qa_actor_id *, qa_body_attachment *);
bool qa_world_transport_attachments(qa_world *, qa_error *);
/* An override affects this link snapshot only; movement precision remains in
 * the authoritative body. Link never dispatches trigger callbacks implicitly. */
bool qa_world_link(qa_world *, qa_actor_id, const qa_vec3 *origin_override, qa_error *);
bool qa_world_unlink(qa_world *, qa_actor_id, qa_error *);
bool qa_world_linked(const qa_world *, qa_actor_id, qa_linked_body *);
bool qa_world_link_state(const qa_world *, qa_actor_id, qa_body_link_state *);
bool qa_world_restore_link_state(qa_world *, qa_actor_id, const qa_body_link_state *, qa_error *);

typedef enum qa_spatial_visit { QA_SPATIAL_CONTINUE, QA_SPATIAL_STOP_SECTOR, QA_SPATIAL_STOP } qa_spatial_visit;
typedef qa_spatial_visit (*qa_spatial_visit_fn)(void *, const qa_spatial_actor *);
/* Retained broadphase bounds, current authoritative body/collision for output.
 * Visits preserve sector order and family-specific insertion order. */
bool qa_world_visit(qa_world *, qa_bounds, qa_collision_role, qa_spatial_visit_fn, void *, qa_error *);
bool qa_world_query(qa_world *, qa_bounds, qa_collision_role, qa_actor_id *, size_t capacity, size_t *count, bool *overflow, qa_error *);
bool qa_world_trace(qa_world *, const qa_trace_query *, qa_trace_result *, qa_error *);
bool qa_world_trace_excluding(qa_world *, const qa_trace_query *, const qa_actor_id *, size_t count, qa_trace_result *, qa_error *);
bool qa_world_point_contents(qa_world *, const qa_point_query *, qa_point_contents *, qa_error *);

typedef struct qa_touch_contact {
    qa_actor_id self, other;
    bool has_plane, has_surface;
    qa_collision_plane plane;
    qa_collision_surface surface;
    /* Q2 rerelease can observe the complete source trace and inversion. */
    bool has_source_trace, inverted;
    qa_trace_result source_trace;
} qa_touch_contact;
typedef void (*qa_world_touch_fn)(void *, qa_world *, const qa_touch_contact *);
typedef bool (*qa_world_is_trigger_fn)(void *, qa_actor_id);
/* Q1 traverses live sector links; Q2/Q3 snapshot candidate IDs before callbacks.
 * All families recheck generations and current overlap after nested mutation. */
bool qa_world_touch_triggers(qa_world *, qa_actor_id, qa_collision_family, qa_world_is_trigger_fn, qa_world_touch_fn, void *, qa_error *);
int32_t qa_world_actor_contents(const qa_actor_collision *, qa_collision_family);

#endif
