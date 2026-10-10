#ifndef QA_UNIFIED_FRAME_COMPONENTS_H
#define QA_UNIFIED_FRAME_COMPONENTS_H

#include "qa/network_unified_frame.h"
#include "qa/network_q3.h"
#include "qa/qvm.h"


typedef struct qa_unified_component_binding {
    uint32_t slot;
    qa_actor_id actor;
    bool owned;
} qa_unified_component_binding;
typedef struct qa_unified_component_source {
    qa_source_owner owner;
    qa_qvm_abi abi;
    qa_actor_id viewer;
    int32_t client_number;
    int64_t game_state_revision;
    bool weapon_presented, scene;
    int64_t scene_revision;
    qa_q3_snapshot snapshot;
    qa_unified_component_binding *bindings;
    size_t binding_count;
} qa_unified_component_source;
typedef struct qa_unified_native_hud {
    int16_t stats[64];
    size_t stat_count;
    int32_t server_frame;
    double time_ms, frame_time_ms;
    bool has_frame_time;
} qa_unified_native_hud;
typedef struct qa_unified_native_camera {
    double origin[3], angles[3], view_height, kick_angles[3], field_of_view;
    double blend[4], damage_blend[4];
    bool rerelease;
    qa_vec3 movement_origin;
    uint32_t render_flags;
    bool position_prediction, angular_prediction, weapon_visible;
} qa_unified_native_camera;
typedef struct qa_unified_native_component {
    qa_source_owner owner;
    uint64_t generation;
    qa_actor_id viewer;
    qa_unified_native_hud *hud;
    qa_unified_native_camera *view;
} qa_unified_native_component;
struct qa_unified_frame_components {
    uint64_t revision;
    qa_unified_native_component *native;
    size_t native_count;
    qa_unified_component_source *sources;
    size_t source_count;
};

#endif
