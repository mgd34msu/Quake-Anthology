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
typedef struct qa_collision_binding {
    void *context;
    bool (*read)(void *, qa_actor_collision *, qa_error *);
} qa_collision_binding;
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
typedef enum qa_world_leaf_policy { QA_WORLD_LEAVES_BOX, QA_WORLD_LEAVES_Q1_TOUCHED } qa_world_leaf_policy;
typedef struct qa_world_leaf_membership {
    const qa_collision_leaf *leaves;
    size_t count;
    int32_t topnode;
    uint32_t last_leaf;
} qa_world_leaf_membership;
/* Borrowed until this body's next membership preparation, replacement or
 * release. NULL bounds reads its published link; explicit bounds prepare the
 * same derived owner before Source link publication. Q1_TOUCHED preserves
 * original Q1 visibility classification and solid-leaf exclusion. */
bool qa_world_link_membership(qa_world *, qa_actor_id, const qa_bounds *,
    qa_world_leaf_policy, qa_world_leaf_membership *, qa_error *);

/* The session owns actors and geometry. World borrows both; destruction never
 * clears/releases the registry. Calls and callbacks have one thread owner.
 * Callbacks may mutate actors/links; destroying the world within one is rejected. */
bool qa_world_create(qa_actor_registry *, qa_collision_geometry *, const qa_world_hooks *, qa_world **, qa_error *);
/* False for NULL or during a world callback/spatial visit. Geometry admissions
 * have separate ownership and must still be aborted before world destruction. */
bool qa_world_idle(const qa_world *);
bool qa_world_destroy(qa_world *, qa_error *);
qa_actor_registry *qa_world_actors(qa_world *);
qa_collision_geometry *qa_world_geometry(qa_world *);
typedef struct qa_world_geometry_admission qa_world_geometry_admission;
/* Geometry remains borrowed. Prepare leaves the current world untouched;
 * validate/commit require an empty registry and all body releases forwarded.
 * Commit preserves the world pointer, allocates/calls nothing, and consumes
 * success. Abort consumes a pending token. Close tokens before the world. */
bool qa_world_prepare_geometry(qa_world *, qa_collision_geometry *, qa_world_geometry_admission **, qa_error *);
bool qa_world_geometry_admission_validate(qa_world_geometry_admission *, qa_error *);
bool qa_world_geometry_admission_commit(qa_world_geometry_admission *, qa_error *);
void qa_world_geometry_admission_abort(qa_world_geometry_admission *);
/* The session must forward every registry release here, after invalidation,
 * before provider teardown. It unlinks the released generation and releases
 * attached children through the same registry, including nested attachments. */
bool qa_world_actor_released(qa_world *, qa_actor_record, qa_error *);
bool qa_world_body_create(qa_world *, qa_actor_id, const qa_body_state *, qa_error *);
bool qa_world_body_bind(qa_world *, qa_actor_id, const qa_body_binding *, bool replace, qa_error *);
uint64_t qa_world_body_storage_serial(const qa_world *, qa_actor_id);
bool qa_world_body_read(qa_world *, qa_actor_id, qa_body_state *, qa_error *);
bool qa_world_body_write(qa_world *, qa_actor_id, const qa_body_state *, qa_error *);
/* Stored policy is used when no binding is present; NULL disables that policy.
 * Binding context is borrowed until explicit unbind (NULL) or actor release.
 * Neither operation relinks. Reads use current metadata with retained bounds. */
bool qa_world_set_collision(qa_world *, qa_actor_id, const qa_actor_collision *, qa_error *);
bool qa_world_collision_bind(qa_world *, qa_actor_id, const qa_collision_binding *, qa_error *);
/* Idle teardown removes only the expected context. Absent/retired actors or a
 * replacement context are successful no-ops; no collision/link state changes. */
bool qa_world_collision_unbind(qa_world *, qa_actor_id, void *expected_context, qa_error *);
/* False with no error means absent; reader/validation failures set an error. */
bool qa_world_get_collision(qa_world *, qa_actor_id, qa_actor_collision *, qa_error *);
/* Link metadata does not require an initialized clipping hull. Only the bound
 * reader for this exact actor observes this purpose; nested queries are strict. */
bool qa_world_get_link_collision(qa_world *, qa_actor_id, qa_actor_collision *, qa_error *);
bool qa_world_collision_link_observation(const qa_world *, qa_actor_id);
bool qa_world_collision_validate(qa_world *, const qa_actor_collision *, qa_error *);
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
/* Publish provider-computed broadphase bounds with current authoritative body,
 * normal link count and notifications. Bypasses the global bounds hook only. */
bool qa_world_link_bounds(qa_world *, qa_actor_id, const qa_bounds *, qa_error *);
/* Explicit absolute bounds with an optional link-only origin override. Neither
 * changes the authoritative body; link count and notifications are normal. */
bool qa_world_link_bounds_at(qa_world *, qa_actor_id, const qa_bounds *absolute_bounds,
                             const qa_vec3 *origin_override, qa_error *);
bool qa_world_unlink(qa_world *, qa_actor_id, qa_error *);
/* Remove collision membership without changing the linked snapshot/count or
 * invoking unlink hooks. A subsequent normal link publishes collision again. */
bool qa_world_suspend_collision(qa_world *, qa_actor_id, qa_error *);
bool qa_world_linked(const qa_world *, qa_actor_id, qa_linked_body *);
bool qa_world_link_state(const qa_world *, qa_actor_id, qa_body_link_state *);
bool qa_world_restore_link_state(qa_world *, qa_actor_id, const qa_body_link_state *, qa_error *);

typedef struct qa_world_body_checkpoint {
    qa_saved_actor_id actor, ground, stored_ground, linked_ground, collision_owner,
                      stored_collision_owner, retained_collision_owner, anchor;
    qa_body_state state, stored_state;
    qa_body_link_state link;
    qa_actor_collision collision, stored_collision, retained_collision;
    qa_body_attachment attachment;
    uint64_t storage_serial, collision_serial, attachment_order;
    bool external_body, external_collision, has_collision, effective_collision, attached;
    bool has_ground, has_stored_ground, has_linked_ground, has_collision_owner,
         has_stored_collision_owner, has_retained_collision_owner, has_anchor;
} qa_world_body_checkpoint;
typedef struct qa_world_spatial_checkpoint {
    qa_saved_actor_id actor;
    uint32_t sector;
} qa_world_spatial_checkpoint;
typedef struct qa_world_checkpoint {
    qa_world_body_checkpoint *bodies;
    qa_world_spatial_checkpoint *spatial; /* Sector list order, including Q3 head insertion. */
    size_t body_count, spatial_count;
    uint64_t attachment_order, body_serial;
} qa_world_checkpoint;
/* Capture keeps live state distinct from retained link/collision snapshots and
 * captures suspended membership. Callback addresses never enter this value.
 * Restore requires an isolated candidate after providers recreate bindings.
 * Failure may leave that candidate partially restored; discard it. External
 * binding kind and effective authoritative state must match the saved owner. */
bool qa_world_checkpoint_capture(qa_world *, qa_world_checkpoint *, qa_error *);
bool qa_world_checkpoint_restore(qa_world *, const qa_world_checkpoint *, qa_error *);
void qa_world_checkpoint_free(qa_world_checkpoint *);

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
