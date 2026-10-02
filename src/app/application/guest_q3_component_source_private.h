#ifndef QA_APPLICATION_GUEST_Q3_COMPONENT_SOURCE_PRIVATE_H
#define QA_APPLICATION_GUEST_Q3_COMPONENT_SOURCE_PRIVATE_H
#include "guest_q3_component_source.h"

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
typedef struct component_source_command { int32_t sequence; qa_actor_id recipient; char *text; } component_source_command;
typedef struct component_source_publication {
    int64_t revision,game_state_revision;
    int32_t time_ms,command_sequence;
    qa_q3_gamestate *game_state;
    component_source_entity *entities;
    size_t entity_count;
    component_source_client *clients;
    size_t client_count;
    component_source_command *commands;
    size_t command_count;
} component_source_publication;
typedef struct component_source_borrow {
    struct component_source_borrow *next;
    application_q3_component_view *view;
    application_q3_scene_context context;
    qa_q3_snapshot snapshot;
    qa_q3_visible_entities visible;
    application_q3_scene_actor *actors;
    application_q3_scene_command *commands;
} component_source_borrow;
struct application_q3_component_source {
    application_q3_component_source_options options;
    component_source_actor *actors;
    size_t actor_count;
    qa_q3_gamestate *game_state;
    int64_t game_state_revision,revision;
    int32_t time_ms,command_sequence;
    component_source_command commands[64];
    component_source_publication current,baseline;
    component_source_borrow *borrows;
    bool dirty;
};
#endif
