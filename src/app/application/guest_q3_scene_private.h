#ifndef QA_APPLICATION_GUEST_Q3_SCENE_PRIVATE_H
#define QA_APPLICATION_GUEST_Q3_SCENE_PRIVATE_H
#include "guest_q3_scene.h"
#include "qa/binary.h"
#include <stdlib.h>
#include <string.h>

typedef struct q3scene_snapshot {
    int32_t number;
    qa_q3_snapshot value;
    qa_q3_entity *entities;
} q3scene_snapshot;
typedef struct q3scene_command { int32_t sequence; qa_command_tokens tokens; } q3scene_command;
struct application_q3_scene {
    application_q3_scene_options options;
    qa_q3_host *host;
    qa_qvm *vm;
    qa_qvm_options lower;
    application_q3_component_body *body;
    qa_qvm_binding event_binding;
    application_q3_scene_context context;
    application_q3_scene_context body_context;
    qa_q3_gamestate *game_state;
    qa_actor_id *players;
    application_q3_scene_actor *actors;
    size_t actor_count;
    qa_buffer defaults;
    q3scene_snapshot snapshots[32];
    q3scene_command commands[64];
    qa_command_tokens reached;
    const qa_command_tokens *lexical;
    int32_t snapshot_number;
    int64_t revision, scene_revision;
    uint64_t frame, hud_frame;
    uint64_t event_sequence;
    const application_q3_scene_player_event *active_event;
    bool event_present, frame_present, hud_present, initialized, failed, busy, restoring_scene, acquired;
};
bool q3scene_fail(qa_error *, qa_status, const char *);
bool q3scene_current(const application_q3_scene *);
void q3scene_history_clear(application_q3_scene *);
#endif
