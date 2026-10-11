#ifndef QA_UNIFIED_FRAME_Q3_H
#define QA_UNIFIED_FRAME_Q3_H

#include "qa/network_unified_frame.h"
#include "qa/network_q3.h"

typedef struct qa_unified_q3_entity {
    uint32_t number;
    qa_actor_id actor;
    qa_q3_entity state;
    qa_vec3 origin;
    bool linked;
    uint32_t server_flags;
    int32_t single_client;
    qa_bounds link_bounds;
} qa_unified_q3_entity;
typedef struct qa_unified_q3_client {
    uint32_t source_number;
    int32_t client_slot;
    qa_actor_id actor;
    qa_q3_player state;
} qa_unified_q3_client;
typedef struct qa_unified_q3_visibility {
    uint8_t area_mask[32];
    int32_t *entities;
    size_t entity_count;
} qa_unified_q3_visibility;
typedef struct qa_unified_q3_source {
    qa_string_id provider_name, instance, content;
    uint64_t publication, map_revision, configuration_revision;
    qa_q3_product product;
    int32_t server_time, level_start, game_type;
    uint32_t max_clients;
    uint8_t snapshot_bit;
    qa_actor_id viewer;
    bool has_client;
    uint32_t client_number;
    qa_unified_q3_entity *entities;
    size_t entity_count;
    qa_unified_q3_client *clients;
    size_t client_count;
    qa_unified_q3_visibility *visibility;
} qa_unified_q3_source;
struct qa_unified_frame_q3 {
    qa_unified_q3_source *sources;
    size_t source_count;
};

#endif
