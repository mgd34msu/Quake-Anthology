#ifndef QA_WORLD_INTERNAL_H
#define QA_WORLD_INTERNAL_H
#include "qa/world.h"
#include "qa/arena.h"
#include "internal.h"
#include "../actors_internal.h"

#define QA_SPATIAL_SECTORS 31u
#define QA_SPATIAL_NONE UINT32_MAX
typedef struct qa_spatial_sector {
    int axis;
    float distance;
    uint32_t front, back;
    uint32_t head, tail;
} qa_spatial_sector;
typedef struct qa_spatial_cursor {
    struct qa_spatial_cursor *outer;
    uint32_t next;
} qa_spatial_cursor;
typedef struct qa_world_snapshot_frame {
    struct qa_world_snapshot_frame *next;
    qa_actor_id *actors;
    size_t capacity;
    bool active;
} qa_world_snapshot_frame;
typedef struct qa_world_actor_snapshot {
    qa_actor_id local[8], *actors;
    size_t count, capacity;
    qa_world *world;
    qa_world_snapshot_frame *frame;
    qa_error *error;
    bool failed;
} qa_world_actor_snapshot;
typedef struct qa_world_trace_geometry {
    struct qa_world_trace_geometry *next;
    qa_collision_geometry *geometry;
    qa_trace_scratch *scratch;
} qa_world_trace_geometry;
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
    qa_world_snapshot_frame *snapshot_frames;
    qa_world_snapshot_frame *free_snapshot_frames;
    qa_arena snapshot_storage;
    size_t snapshot_frame_capacity, snapshot_active, snapshot_peak;
};
static inline qa_collision_geometry *qa_world_model_geometry(const qa_world *world,
    const qa_actor_collision *collision)
{ return collision->model_geometry ? collision->model_geometry : world->geometry; }
qa_world_body *qa_world_find_body(const qa_world *, qa_actor_id);
bool qa_world_body_sample(qa_world_body *, qa_entity_pose,
                          qa_entity_body_components, qa_body_state *, qa_error *);
bool qa_world_collision_sample(const qa_world_body *, bool,
                               qa_entity_collision_components, qa_actor_collision *, qa_error *);
static inline qa_world_body *qa_world_raw_body(const qa_world *world, uint32_t slot)
{
    if(world==NULL || slot>=world->capacity) return NULL;
    qa_world_body *body=qa_actors_body(world->actors->pages,slot);
    return body->world==NULL || body->world==world?body:NULL;
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
