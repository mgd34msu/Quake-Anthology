#ifndef QA_APPLICATION_GUEST_Q3_COMPONENT_BODY_PRIVATE_H
#define QA_APPLICATION_GUEST_Q3_COMPONENT_BODY_PRIVATE_H
#include "guest_q3_component_body.h"

typedef struct component_body_output {
    qa_actor_id actor;
    qa_application_q3_component_part *parts;
    size_t count,capacity;
    size_t *pass_capacities;
} component_body_output;
typedef struct component_player_scope {
    struct component_player_scope *previous;
    component_body_output output;
    bool *pending;
    int64_t state;
    uint32_t physical;
    bool admitted;
} component_player_scope;
typedef struct component_mesh_scope {
    struct component_mesh_scope *previous;
    component_player_scope *player;
    size_t part;
    int32_t pointer, shader;
} component_mesh_scope;
struct qa_application_q3_component_body_lease {
    application_q3_component_body *owner;
    struct qa_application_q3_component_body_lease *next;
};
struct application_q3_component_body {
    application_q3_component_body_options options;
    qa_qvm_binding bindings[2];
    component_player_scope *player;
    component_mesh_scope *mesh;
    component_body_output *outputs;
    qa_unified_frame_lease *storage;
    qa_application_q3_component_body_lease *leases;
    size_t count,capacity;
    uint64_t sequence, generation;
    int32_t time_ms;
    bool entered, completed, busy;
};
#endif
