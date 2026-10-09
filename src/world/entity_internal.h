#ifndef QA_ENTITY_INTERNAL_H
#define QA_ENTITY_INTERNAL_H

#include "qa/world.h"

typedef struct qa_spatial_link {
    qa_bounds bounds;
    uint32_t previous, next, sector;
    bool linked;
} qa_spatial_link;

typedef struct qa_world_body {
    qa_world *world;
    qa_actor_id actor;
    uint64_t storage_serial;
    bool present, external, has_collision, attached, linked;
    qa_body_state state;
    qa_body_binding binding;
    qa_actor_collision collision;
    qa_collision_binding collision_binding;
    uint64_t collision_serial;
    qa_body_attachment attachment;
    uint64_t attachment_order;
    qa_body_state linked_state;
    qa_actor_collision linked_collision;
    uint64_t link_count;
    qa_collision_leaf *leaves;
    size_t leaf_count, leaf_capacity;
    qa_bounds leaf_bounds;
    qa_collision_geometry *leaf_geometry;
    uint64_t leaf_storage;
    int32_t leaf_topnode;
    uint32_t leaf_last;
    qa_world_leaf_policy leaf_policy;
    bool leaves_ready;
} qa_world_body;

void qa_world_reset_bodies(qa_world *);
bool qa_world_collision_rows_validate(qa_world *, const qa_spatial_actor *, size_t, qa_error *);

#endif
