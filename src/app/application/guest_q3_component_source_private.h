#ifndef QA_APPLICATION_GUEST_Q3_COMPONENT_SOURCE_PRIVATE_H
#define QA_APPLICATION_GUEST_Q3_COMPONENT_SOURCE_PRIVATE_H
#include "guest_q3_component_source.h"
#include "qa/actors.h"

typedef struct component_source_actor {
    application_q3_scene_actor row;
    bool client;
} component_source_actor;
typedef struct component_source_entity {
    application_q3_scene_actor actor;
    qa_q3_entity state;
    qa_qvm_entity_shared shared;
    qa_q3_host_visibility visibility;
} component_source_entity;
typedef struct component_source_client { qa_actor_id actor; uint32_t slot; qa_q3_player state; } component_source_client;
typedef struct component_source_publication {
    bool ready;
    int64_t revision,game_state_revision;
    int32_t time_ms,command_sequence;
    qa_q3_gamestate *game_state;
    component_source_entity *entities;
    size_t entity_count;
    component_source_client *clients;
    size_t client_count;
    qa_actor_index client_rows;
} component_source_publication;
typedef struct component_source_borrow {
    struct component_source_borrow *next;
    application_q3_component_view *view;
    qa_unified_frame_lease *storage;
    application_q3_scene_context context;
    qa_q3_snapshot snapshot;
    qa_q3_visible_entities visible;
    application_q3_scene_actor *actors;
    application_q3_scene_command commands[64];
    size_t command_count;
    uint64_t command_next;
} component_source_borrow;
struct application_q3_component_source {
    application_q3_component_source_options options;
    component_source_actor *actors;
    size_t actor_count;
    qa_id_index actor_slots;
    qa_actor_index actor_rows;
    qa_q3_gamestate *game_state;
    int64_t game_state_revision,revision;
    int32_t time_ms,command_sequence;
    component_source_publication current,baseline,pending;
    qa_stamp_set visibility_marks;
    uint32_t visibility_storage[QA_Q3_ENTITIES];
    component_source_borrow *borrows;
    bool dirty,restored;
};
bool q3component_source_indexes_rebuild(application_q3_component_source *,qa_error *);
#endif
