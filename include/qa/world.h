#ifndef QA_WORLD_H
#define QA_WORLD_H

#include "qa/collision.h"

typedef struct qa_world qa_world;
typedef struct qa_body_state {
    qa_vec3 origin, angles, velocity;
    qa_bounds bounds;
    qa_actor_reference ground;
} qa_body_state;
typedef struct qa_entity_visual {
    qa_string_id models[4];
    int32_t frame, old_frame, skin, colormap;
    uint64_t effects;
    uint32_t render_flags, inline_model;
    float scale, alpha;
    uint8_t player_colors;
    bool visible, has_inline_model, has_player_colors;
} qa_entity_visual;
typedef enum qa_body_vector_kind {
    QA_BODY_ORIGIN = offsetof(qa_body_state, origin),
    QA_BODY_VELOCITY = offsetof(qa_body_state, velocity),
    QA_BODY_ANGLES = offsetof(qa_body_state, angles),
    QA_BODY_MINIMUM = offsetof(qa_body_state, bounds.mins),
    QA_BODY_MAXIMUM = offsetof(qa_body_state, bounds.maxs)
} qa_body_vector_kind;
/* Module bindings select the field at load; encodings remain at the boundary. */
static inline qa_vec3 *qa_body_vector(qa_body_state *body, qa_body_vector_kind kind)
{ return (qa_vec3 *)((uint8_t *)body + kind); }
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
    /* Borrowed from the model owner; NULL selects the shared map geometry.
     * This derived handle is rebound from installed content on restore. */
    qa_collision_geometry *model_geometry;
    qa_collision_bits contents;
    int32_t q1_opaque_token;
    qa_actor_reference owner;
    qa_collision_role role;
    bool monster, dead_monster, q1_corpse;
    bool has_q3_owner;
    int32_t q3_entity_number, q3_owner_number;
} qa_actor_collision;
typedef enum qa_entity_pose {
    QA_ENTITY_CONTROL_POSE, QA_ENTITY_CLIP_POSE, QA_ENTITY_CONTENTS_POSE,
    QA_ENTITY_POSE_COUNT
} qa_entity_pose;
typedef enum qa_entity_body_components {
    QA_ENTITY_BODY_SPATIAL, QA_ENTITY_BODY_ALL
} qa_entity_body_components;
typedef enum qa_entity_scalar_encoding {
    QA_ENTITY_NO_FIELD, QA_ENTITY_F32_LE, QA_ENTITY_I32_LE,
    QA_ENTITY_U32_LE, QA_ENTITY_U8, QA_ENTITY_U64_LE
} qa_entity_scalar_encoding;
typedef struct qa_entity_scalar_field {
    const uint8_t *bytes;
    qa_entity_scalar_encoding encoding;
} qa_entity_scalar_field;
typedef struct qa_entity_vector_field { const uint8_t *word[3]; } qa_entity_vector_field;
/* A borrowed view of the module's existing slot projection. It adds no actor
 * identities. Count and rows stay current at module admission/release sites. */
typedef struct qa_entity_references {
    qa_actor_registry *actors;
    qa_actor_owner owner;
    uint64_t base, stride;
    const uint8_t *slots;
    const uint32_t *count;
    uint32_t capacity, slot_stride, actor_offset, kind_offset;
    qa_entity_scalar_encoding kind_encoding;
    uint32_t borrowed_kind;
    bool foreign_owner, zero_is_none, invalid_is_none;
    bool has_world_number, has_none_number;
    int32_t world_number, none_number;
    qa_actor_reference world;
} qa_entity_references;
typedef struct qa_entity_body_fields {
    struct { qa_entity_vector_field origin, angles; } pose[QA_ENTITY_POSE_COUNT];
    qa_entity_vector_field velocity, minimum, maximum;
    qa_entity_scalar_field ground;
    const qa_entity_references *references;
    bool spatial_complete;
} qa_entity_body_fields;
typedef struct qa_entity_model_field {
    uint32_t model;
    qa_collision_geometry *geometry;
    bool present;
} qa_entity_model_field;
typedef struct qa_entity_model_fields {
    const qa_entity_model_field *entries;
    uint32_t count;
} qa_entity_model_fields;
typedef struct qa_entity_collision_fields {
    qa_collision_family family;
    bool rerelease;
    int32_t entity_number;
    qa_entity_scalar_field solid, flags, model, owner, contents, brush_model;
    const qa_entity_references *references;
    const qa_entity_model_fields *models;
} qa_entity_collision_fields;
typedef enum qa_entity_collision_components {
    QA_ENTITY_COLLISION_ROLE, QA_ENTITY_COLLISION_ALL
} qa_entity_collision_components;
/* Cold adapters resolve addresses and ABI rules. Sampling only reads those
 * bytes; it never enters a module, resolves names or observes OS mappings. */
qa_entity_vector_field qa_entity_vector_bytes(const void *);
/* Derive layout metadata after cold address binding; zeroed or sparse fields
 * retain the nullable sampler. Values are always read from the live words. */
void qa_entity_body_fields_prepare(qa_entity_body_fields *);
/* SPATIAL writes only origin, angles and bounds; ALL writes the complete state
 * transactionally, so a failed reference read leaves the output unchanged. */
bool qa_entity_body_read(const qa_entity_body_fields *, qa_entity_pose,
                         qa_entity_body_components, qa_body_state *, qa_error *);
/* ROLE writes only role; ALL writes the complete collision record. */
bool qa_entity_collision_read(const qa_entity_collision_fields *, bool linking,
                              qa_entity_collision_components, qa_actor_collision *, qa_error *);
typedef struct qa_collision_binding {
    void *context;
    const qa_entity_collision_fields *fields;
} qa_collision_binding;
typedef struct qa_spatial_actor { qa_linked_body body; qa_actor_collision collision; } qa_spatial_actor;
typedef struct qa_body_binding {
    void *context;
    const qa_entity_body_fields *fields;
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
typedef struct qa_leaf_area_pair { int32_t area, area2; uint16_t count; } qa_leaf_area_pair;
typedef struct qa_world_leaf_visibility_result {
    size_t count;
    int32_t topnode, last_cluster, last_emitted_cluster, maximum_cluster;
    uint32_t last_leaf;
    qa_leaf_area_pair q2_areas, native_areas, foreign_areas, unique_areas, portal_areas;
    int32_t ordered[16], sorted[16], distinct[128];
    uint16_t ordered_count, sorted_count, distinct_count;
    bool prefix_invalid, all_invalid, last_invalid, last_emitted_invalid, portal_invalid, portal_third;
    uint32_t q1_leaves[16];
    uint16_t q1_count;
    qa_bytes cluster_bits, area_bits;
    int32_t first_cluster, invalid_cluster, invalid_area;
    bool has_invalid_cluster, has_invalid_area;
} qa_world_leaf_visibility_result;
/* Derived geometry only, borrowed until this body's next changed query/release.
 * BOX aggregates every occurrence, retaining exact bounded source projections.
 * Q1_TOUCHED independently retains the original non-solid first16 leaf IDs. */
bool qa_world_leaf_visibility(qa_world *, qa_actor_id, const qa_bounds *,
    qa_world_leaf_policy, qa_trace_scratch *, const qa_world_leaf_visibility_result **, qa_error *);
bool qa_world_q1_visible(qa_world *, qa_actor_id, const qa_bounds *, qa_bytes pvs,
    bool *, qa_error *);

/* The session owns actors and the map geometry. Foreign model registration
 * retains its geometry with the world's scratch. Destruction never clears
 * or releases the registry. Calls and callbacks have one thread owner.
 * Callbacks may mutate actors/links; destroying the world within one is rejected. */
enum { QA_WORLD_SNAPSHOT_DEFAULT_FRAMES = 32 };
/* A zero frame capacity selects the default bounded nesting budget. Snapshot
 * storage is sized to actor capacity at load and never grows during queries. */
bool qa_world_create(qa_actor_registry *, qa_collision_geometry *, const qa_world_hooks *,
                     size_t snapshot_frame_capacity, qa_world **, qa_error *);
typedef struct qa_world_snapshot_usage {
    size_t capacity, active, peak, overflow;
} qa_world_snapshot_usage;
qa_world_snapshot_usage qa_world_snapshot_statistics(const qa_world *);
/* False for NULL or during a world callback/spatial visit. Geometry admissions
 * have separate ownership and must still be aborted before world destruction. */
bool qa_world_idle(const qa_world *);
bool qa_world_destroy(qa_world *, qa_error *);
qa_actor_registry *qa_world_actors(qa_world *);
qa_collision_geometry *qa_world_geometry(qa_world *);
/* Prepare foreign brush models at load or model registration. Query access is
 * a plain lookup and never creates scratch. The world owns its scratch. */
bool qa_world_prepare_trace_geometry(qa_world *, qa_collision_geometry *, qa_error *);
qa_trace_scratch *qa_world_trace_scratch(qa_world *, const qa_collision_geometry *);
typedef struct qa_world_geometry_admission qa_world_geometry_admission;
/* Geometry remains borrowed. Prepare leaves the current world untouched;
 * validate/commit require an empty registry and all body releases forwarded.
 * Commit preserves the world pointer, publishes prepared scratch without allocation, and consumes
 * success. Abort consumes a pending token. Close tokens before the world. */
bool qa_world_prepare_geometry(qa_world *, qa_collision_geometry *, qa_world_geometry_admission **, qa_error *);
bool qa_world_geometry_admission_validate(qa_world_geometry_admission *, qa_error *);
bool qa_world_geometry_admission_commit(qa_world_geometry_admission *, qa_error *);
void qa_world_geometry_admission_abort(qa_world_geometry_admission *);
/* The registry forwards release to the canonical body owner after invalidation,
 * before observer teardown. It unlinks the released generation and releases
 * attached children through the same registry, including nested attachments. */
bool qa_world_actor_released(qa_world *, qa_actor_record, qa_error *);
bool qa_world_body_create(qa_world *, qa_actor_id, const qa_body_state *, qa_error *);
bool qa_world_body_bind(qa_world *, qa_actor_id, const qa_body_binding *, bool replace, qa_error *);
/* Optional readonly source spatial views overlay CLIP/CONTENTS on a stored
 * body. CONTROL, velocity, ground, writes and checkpoints stay canonical.
 * Fields are borrowed until unbind/release; external writable bindings retain
 * their existing sampler. NULL removes the spatial view. */
bool qa_world_body_spatial_bind(qa_world *, qa_actor_id, const qa_entity_body_fields *, qa_error *);
const qa_entity_body_fields *qa_world_body_spatial_fields(const qa_world *, qa_actor_id);
uint64_t qa_world_body_storage_serial(const qa_world *, qa_actor_id);
bool qa_world_body_read(qa_world *, qa_actor_id, qa_body_state *, qa_error *);
bool qa_world_body_read_pose(qa_world *, qa_actor_id, qa_entity_pose,
                            qa_body_state *, qa_error *);
bool qa_world_body_write(qa_world *, qa_actor_id, const qa_body_state *, qa_error *);
/* Internal movement commit. NULL state retains the authoritative body; a
 * changed state reaches its normal write binding. Optional linking preserves
 * an unchanged retained membership and dispatches no linked notifications.
 * Module LinkEntity requests use qa_world_link/_bounds for forced reinsertion.
 * Trigger dispatch remains separate, including after an unchanged commit. */
bool qa_world_body_commit(qa_world *, qa_actor_id, const qa_body_state *, bool link, qa_error *);
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
/* Link metadata permits a brush awaiting its model index; clipping remains
 * strict. The purpose is passed directly to the shared field sampler. */
bool qa_world_get_link_collision(qa_world *, qa_actor_id, qa_actor_collision *, qa_error *);
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
    qa_actor_reference_kind ground_kind, stored_ground_kind, linked_ground_kind;
    qa_actor_owner ground_owner, stored_ground_owner, linked_ground_owner;
    qa_actor_reference_kind collision_owner_kind, stored_collision_owner_kind, retained_collision_owner_kind;
    qa_actor_owner collision_owner_owner, stored_collision_owner_owner, retained_collision_owner_owner;
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
 * Restore requires an isolated candidate after providers recreate body bindings.
 * The optional binding callback runs after saved bodies are reconstructed and
 * before collision ownership is validated. It may bind existing actors only.
 * Failure may leave that candidate partially restored; discard it. External
 * binding kind and effective authoritative state must match the saved owner. */
bool qa_world_checkpoint_capture(qa_world *, qa_world_checkpoint *, qa_error *);
typedef bool (*qa_world_restore_bindings_fn)(void *, qa_error *);
bool qa_world_checkpoint_restore(qa_world *, const qa_world_checkpoint *,
                                 qa_world_restore_bindings_fn, void *, qa_error *);
void qa_world_checkpoint_free(qa_world_checkpoint *);

typedef enum qa_spatial_visit { QA_SPATIAL_CONTINUE, QA_SPATIAL_STOP_SECTOR, QA_SPATIAL_STOP } qa_spatial_visit;
typedef qa_spatial_visit (*qa_spatial_visit_fn)(void *, const qa_spatial_actor *);
/* Retained broadphase bounds, current authoritative body/collision for output.
 * Visits preserve sector order and family-specific insertion order. */
bool qa_world_visit(qa_world *, qa_bounds, qa_collision_role, qa_spatial_visit_fn, void *, qa_error *);
bool qa_world_query(qa_world *, qa_bounds, qa_collision_role, qa_actor_id *, size_t capacity, size_t *count, bool *overflow, qa_error *);
/* CLIENT membership borrows decoded append order over the common linked
 * bodies. NULL actors retains the normal spatial candidate order. */
typedef enum qa_world_trace_merge {
    QA_WORLD_MERGE_SERVER, QA_WORLD_MERGE_QW_CLIENT, QA_WORLD_MERGE_Q3_CLIENT
} qa_world_trace_merge;
typedef struct qa_world_query_rules {
    const qa_actor_id *actors;
    size_t count;
    bool brush_contents_only, contents_ignore_pass;
    qa_world_trace_merge merge;
} qa_world_query_rules;
void qa_world_set_query_rules(qa_world *, const qa_world_query_rules *);

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
qa_collision_bits qa_world_actor_contents(const qa_actor_collision *);

#endif
