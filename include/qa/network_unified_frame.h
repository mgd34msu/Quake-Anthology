#ifndef QA_NETWORK_UNIFIED_FRAME_H
#define QA_NETWORK_UNIFIED_FRAME_H

#include "qa/network_unified.h"
#include "qa/scheduler.h"
#include "qa/gameplay.h"
#include "qa/inventory.h"
#include "qa/session.h"
#include "qa/network_unified_frame_pool.h"

/* These are copied observations of the actual Source. Actor IDs retain the
 * Source registry; a frontend resolves them through its admitted aliases. */
typedef struct qa_unified_provider_state {
    char *provider, *content;
} qa_unified_provider_state;
typedef struct qa_unified_actor_state {
    qa_actor_id actor;
    char *owner, *definition;
} qa_unified_actor_state;
typedef struct qa_unified_body_state {
    qa_actor_id actor;
    qa_body_state body;
} qa_unified_body_state;
typedef struct qa_unified_inventory_entry {
    char *item;
    double count;
    double capacity;
    qa_inventory_count_policy policy;
} qa_unified_inventory_entry;
typedef struct qa_unified_inventory_state {
    qa_actor_id actor;
    qa_unified_inventory_entry *entries;
    size_t entry_count;
} qa_unified_inventory_state;
typedef struct qa_unified_configuration_state {
    qa_actor_id actor;
    qa_unified_provider_state movement, character, appearance, inventory;
    qa_unified_provider_state *weapons;
    size_t weapon_count;
} qa_unified_configuration_state;
typedef struct qa_unified_resource_state {
    char *content, *path;
    uint64_t byte_length;
} qa_unified_resource_state;


/* This shared world cut can be retained by all recipients of the same actual
 * Source frame. Inventories and private presentation stay in their frame. */
typedef struct qa_unified_world_frame {
    qa_unified_frame_lease *lease;
    size_t references;
    qa_source_frame source;
    double presentation_seconds;
    qa_unified_actor_state *actors;
    size_t actor_count;
    qa_unified_body_state *bodies;
    size_t body_count;
    qa_unified_resource_state *world;
    qa_spatial_actor *collisions;
    size_t collision_count;
    qa_buffer area_bits;
} qa_unified_world_frame;

typedef struct qa_unified_frame_prediction qa_unified_frame_prediction;
typedef struct qa_unified_frame_player qa_unified_frame_player;
typedef struct qa_unified_frame_visuals qa_unified_frame_visuals;
typedef struct qa_unified_frame_q3 qa_unified_frame_q3;
typedef struct qa_unified_frame_components qa_unified_frame_components;

typedef struct qa_unified_frame {
    qa_unified_frame_lease *lease;
    uint32_t epoch;
    int64_t acknowledged_input;
    qa_unified_world_frame *world;
    qa_unified_inventory_state *inventories;
    size_t inventory_count;
    qa_unified_frame_prediction *prediction;
    qa_unified_frame_player *player;
    qa_unified_frame_visuals *visuals;
    qa_unified_frame_q3 *q3;
    qa_unified_frame_components *components;
} qa_unified_frame;

int64_t qa_unified_world_frame_milliseconds(const qa_unified_world_frame *);
qa_unified_world_frame *qa_unified_world_frame_create(qa_unified_frame_pool *, qa_error *);
bool qa_unified_world_frame_retain(qa_unified_world_frame *, qa_error *);
void qa_unified_world_frame_destroy(qa_unified_world_frame *);
qa_unified_frame *qa_unified_frame_create(qa_unified_frame_pool *, qa_error *);
void qa_unified_frame_destroy(qa_unified_frame *);
bool qa_unified_frame_equal(const qa_unified_frame *, const qa_unified_frame *);
/* Transfer succeeds only after typed boundary validation. Failure preserves
 * both caller-owned pointers. No JSON source or index is constructed. */
bool qa_unified_document_create_frame(qa_unified_frame **owned,
    qa_unified_document **out, qa_error *);
const qa_unified_frame *qa_unified_document_frame(const qa_unified_document *);

#endif
