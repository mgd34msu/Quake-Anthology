/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QA_WORLD_INTERNAL_H
#define QA_WORLD_INTERNAL_H
#include "qa/world.h"
#include "internal.h"

#define QA_BODY_PAGE_SHIFT 8u
#define QA_BODY_PAGE_SIZE (1u << QA_BODY_PAGE_SHIFT)
#define QA_SPATIAL_SECTORS 31u
typedef struct qa_spatial_member qa_spatial_member;
typedef struct qa_world_body {
    qa_actor_id actor;
    bool present, external, has_collision, attached, linked;
    qa_body_state state;
    qa_body_binding binding;
    qa_actor_collision collision;
    qa_body_attachment attachment;
    uint64_t attachment_order;
    qa_linked_body link;
    uint64_t link_count;
    qa_spatial_member *member;
} qa_world_body;
typedef struct qa_spatial_sector {
    int axis;
    float distance;
    uint32_t front, back;
    qa_spatial_member *head, *tail;
} qa_spatial_sector;
struct qa_spatial_member {
    qa_spatial_actor actor;
    uint32_t sector;
    qa_spatial_member *previous, *next, *retired_next;
};
struct qa_world {
    qa_actor_registry *actors;
    qa_collision_geometry *geometry;
    qa_world_hooks hooks;
    qa_world_body **pages;
    uint32_t capacity, page_count;
    qa_spatial_sector sectors[QA_SPATIAL_SECTORS];
    qa_spatial_member *retired, *spare_members;
    uint32_t visit_depth, callback_depth;
    uint64_t attachment_order;
};
qa_world_body *qa_world_find_body(const qa_world *, qa_actor_id);
qa_world_body *qa_world_raw_body(const qa_world *, uint32_t);
bool qa_spatial_initialize(qa_world *, qa_bounds, qa_error *);
qa_spatial_member *qa_spatial_prepare(qa_world *, const qa_linked_body *, const qa_actor_collision *, qa_error *);
void qa_spatial_publish(qa_world *, qa_world_body *, qa_spatial_member *);
void qa_spatial_remove(qa_world *, qa_world_body *);
void qa_spatial_dispose(qa_world *);
typedef qa_spatial_visit (*qa_spatial_raw_fn)(void *, const qa_spatial_actor *);
bool qa_spatial_visit_raw(qa_world *, qa_bounds, qa_spatial_raw_fn, void *, qa_error *);
bool qa_world_refresh(qa_world *, const qa_spatial_actor *, qa_spatial_actor *, qa_error *);

#endif
