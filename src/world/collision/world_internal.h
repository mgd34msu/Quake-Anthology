#ifndef QA_WORLD_INTERNAL_H
#define QA_WORLD_INTERNAL_H
#include "qa/world.h"
#include "qa/pool.h"
#include "internal.h"
#include "../actors_internal.h"

#define QA_SPATIAL_SECTORS 31u
#define QA_SPATIAL_NONE UINT32_MAX
typedef struct qa_spatial_sector {
    int axis;
    float distance;
    uint32_t front, back;
    uint32_t head, tail;
    uint32_t parent, subtree_links;
} qa_spatial_sector;
typedef struct qa_spatial_cursor {
    struct qa_spatial_cursor *outer;
    uint32_t next;
} qa_spatial_cursor;
typedef struct qa_world_snapshot_frame {
    qa_actor_id *actors;
    size_t capacity, slot;
} qa_world_snapshot_frame;
typedef struct qa_world_actor_snapshot {
    qa_actor_id local[8], *actors;
    size_t count, capacity;
    qa_world *world;
    qa_world_snapshot_frame *frame;
    qa_error *error;
    bool failed;
} qa_world_actor_snapshot;
typedef struct qa_world_release_frame {
    uint32_t head, tail;
} qa_world_release_frame;
typedef struct qa_world_release_ticket {
    qa_actor_id actor;
    uint64_t order;
    qa_world_release_frame *frame;
    uint32_t previous, next;
} qa_world_release_ticket;
typedef struct qa_world_trace_geometry {
    struct qa_world_trace_geometry *next;
    qa_collision_geometry *geometry;
    qa_trace_scratch *scratch;
} qa_world_trace_geometry;
typedef struct qa_world_leaf_cache {
    qa_world_leaf_visibility_result leaf_visibility;
    qa_bounds leaf_box_bounds, leaf_q1_bounds;
    qa_collision_geometry *leaf_geometry;
    uint64_t leaf_storage;
    bool leaf_box_ready, leaf_q1_ready;
} qa_world_leaf_cache;
struct qa_world {
    qa_actor_registry *actors;
    qa_collision_geometry *geometry;
    qa_trace_scratch *trace_scratch;
    qa_world_trace_geometry *trace_geometries;
    qa_world_hooks hooks;
    uint32_t capacity;
    qa_spatial_sector sectors[QA_SPATIAL_SECTORS];
    qa_spatial_cursor *cursors;
    uint32_t visit_depth, callback_depth;
    uint64_t attachment_order, body_serial;
    qa_world_geometry_admission *geometry_admission;
    qa_arena visibility_storage;
    qa_world_leaf_cache *leaf_cache;
    uint8_t *visibility_bits;
    size_t cluster_bytes, area_bytes, visibility_stride;
    qa_arena snapshot_storage;
    qa_pool snapshot_pool;
    qa_world_release_ticket *release_tickets;
    size_t release_pending, release_peak;
    qa_world_query_rules query_rules;
};
static inline qa_collision_geometry *qa_world_model_geometry(const qa_world *world,
    const qa_actor_collision *collision)
{ return collision->model_geometry ? collision->model_geometry : world->geometry; }
static inline qa_world_body *qa_world_raw_body(const qa_world *world, uint32_t slot)
{
    if(world==NULL || slot>=world->capacity) return NULL;
    qa_world_body *body=qa_actors_body(world->actors->pages,slot);
    return body->world==NULL || body->world==world?body:NULL;
}

static inline qa_world_body *qa_world_find_body(const qa_world *world, qa_actor_id actor)
{
    qa_world_body *body=qa_world_raw_body(world,actor.slot);
    return body!=NULL && body->present && qa_actor_id_equal(body->actor,actor)
        && qa_actors_get(world->actors,actor)!=NULL?body:NULL;
}

static inline bool qa_world_body_state_valid(const qa_body_state *state,qa_entity_body_components components)
{
    return state!=NULL && qa_vec_finite(state->origin) && qa_vec_finite(state->angles)
        && (components!=QA_ENTITY_BODY_ALL || qa_vec_finite(state->velocity))
        && qa_bounds_valid(state->bounds);
}

static inline bool qa_world_body_sample(qa_world_body *body,qa_entity_pose pose,
                          qa_entity_body_components components,
                          qa_body_state *out,qa_error *error)
{
    qa_body_state state;
    qa_body_state *selected=components==QA_ENTITY_BODY_ALL?&state:out;
    if(body->external) {
        if(!qa_entity_body_read(body->binding.fields,pose,components,selected,error)) return false;
        if(!qa_world_body_state_valid(selected,components)) {
            qa_error_set(error,QA_ERROR_FORMAT,0,"%s","Binding returned invalid body state");
            return false;
        }
        if(components==QA_ENTITY_BODY_ALL) body->state=*selected;
    } else if(components==QA_ENTITY_BODY_ALL) state=body->state;
    else {
        selected->origin=body->state.origin;
        selected->angles=body->state.angles;
        selected->bounds=body->state.bounds;
    }
    if(components==QA_ENTITY_BODY_ALL) *out=*selected;
    return true;
}

static inline bool qa_world_collision_sample(const qa_world_body *body,bool link_metadata,
                               qa_entity_collision_components components,
                               qa_actor_collision *out,qa_error *error)
{
    const qa_entity_collision_fields *fields=body->collision_binding.fields;
    if(fields==NULL) {
        if(!body->has_collision) return false;
        if(components==QA_ENTITY_COLLISION_ROLE) out->role=body->collision.role;
        else *out=body->collision;
        return true;
    }
    return qa_entity_collision_read(fields,link_metadata,components,out,error);
}

static inline qa_linked_body qa_world_published_body(const qa_world *world, const qa_world_body *body)
{
    return (qa_linked_body){body->actor, body->linked_state,
        qa_actors_link(world->actors->links,body->actor.slot)->bounds, body->link_count};
}
bool qa_spatial_initialize(qa_world *, qa_bounds, qa_error *);
void qa_spatial_publish(qa_world *, uint32_t);
void qa_spatial_remove(qa_world *, uint32_t);
void qa_spatial_clear(qa_world *);
void qa_spatial_rebuild_occupancy(qa_world *);
void qa_spatial_dispose(qa_world *);
bool qa_spatial_prepare_snapshots(qa_world *, size_t, qa_error *);
typedef qa_spatial_visit (*qa_spatial_raw_fn)(void *, uint32_t);
bool qa_spatial_visit_raw(qa_world *, qa_bounds, qa_spatial_raw_fn, void *, qa_error *);
bool qa_world_refresh(qa_world *, qa_actor_id, qa_entity_pose,
                      qa_entity_body_components, qa_spatial_actor *, qa_error *);

/* BOTH captures raw links; filtered roles retain current provider readers. */
bool qa_world_snapshot_capture(qa_world *, qa_bounds, qa_collision_role,
                               qa_world_actor_snapshot *, qa_error *);
void qa_world_snapshot_release(qa_world_actor_snapshot *);

#endif
